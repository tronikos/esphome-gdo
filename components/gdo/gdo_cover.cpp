#include "gdo_cover.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace gdo {

static const char *const TAG = "gdo.cover";

const float UNKNOWN_POSITION = 0.5f;
// While an endstop is configured but has not confirmed the travel yet, the
// time-based estimate is held just short of the limit. Reporting a flat 100%
// (or 0%) makes the cover look finished while the door is still moving.
const float ALMOST_OPEN = 0.99f;
const float ALMOST_CLOSED = 0.01f;

using namespace esphome::cover;

// How long to wait for an endstop before giving up on it. The travel-time
// estimate is never exact, so allow a margin over the configured duration.
// Timing out at exactly the duration means a door that takes 12.8s when
// configured for 12.6s glitches to an unknown position on every single cycle.
static uint32_t endstop_timeout(uint32_t duration) { return duration + duration / 4 + 2000; }

void GdoCover::dump_config() {
  LOG_COVER("", "GDO Cover", this);
  LOG_BINARY_SENSOR("  ", "Open Endstop", this->open_endstop_);
  ESP_LOGCONFIG(TAG, "  Open Duration: %.1fs", this->open_duration_ / 1e3f);
  LOG_BINARY_SENSOR("  ", "Close Endstop", this->close_endstop_);
  ESP_LOGCONFIG(TAG, "  Close Duration: %.1fs", this->close_duration_ / 1e3f);
  ESP_LOGCONFIG(TAG, "  Press While Closing: %s",
                this->press_while_closing_ == PRESS_WHILE_CLOSING_STOPS ? "stop" : "open");
}

void GdoCover::setup() {
  // Restore state after restart
  auto restore = this->restore_state_();
  if (restore.has_value()) {
    restore->apply(this);
  } else {
    this->position = UNKNOWN_POSITION;
  }
  if (this->open_endstop_ != nullptr && this->open_endstop_->has_state()) {
    // Fix restored state based on open endstop
    if (this->open_endstop_->state) {
      this->position = COVER_OPEN;
    } else if (this->position == COVER_OPEN) {
      this->position = UNKNOWN_POSITION;
    }
  }
  if (this->close_endstop_ != nullptr && this->close_endstop_->has_state()) {
    // Fix restored state based on closed endstop
    if (this->close_endstop_->state) {
      this->position = COVER_CLOSED;
    } else if (this->position == COVER_CLOSED) {
      this->position = UNKNOWN_POSITION;
    }
  }
  if (this->open_endstop_ != nullptr) {
    this->open_endstop_->add_on_state_callback([this](bool value) {
      if (value) {
        // Reached the open endstop. Update state
        float dur = (millis() - this->start_dir_time_) / 1e3f;
        ESP_LOGI(TAG, "Open endstop reached. Took %.1fs.", dur);
        this->position = COVER_OPEN;
        this->target_position_ = COVER_OPEN;
        this->current_operation = COVER_OPERATION_IDLE;
        this->publish_state();
      } else {
        // Moved away from the open endstop.
        // If this was triggered by an external control assume target position is fully closed
        // and start updating state without triggering a press.
        ESP_LOGI(TAG, "Open endstop released.");
        if (this->current_operation == COVER_OPERATION_IDLE) {
          this->target_position_ = COVER_CLOSED;
          this->start_direction_(COVER_OPERATION_CLOSING, false);
        }
      }
    });
  }
  if (this->close_endstop_ != nullptr) {
    this->close_endstop_->add_on_state_callback([this](bool value) {
      if (value) {
        // Reached the closed endstop. Update state
        float dur = (millis() - this->start_dir_time_) / 1e3f;
        ESP_LOGI(TAG, "Closed endstop reached. Took %.1fs.", dur);
        this->position = COVER_CLOSED;
        this->target_position_ = COVER_CLOSED;
        this->current_operation = COVER_OPERATION_IDLE;
        this->publish_state();
      } else {
        // Moved away from the closed endstop.
        // If this was triggered by an external control assume target position is fully open
        // and start updating state without triggering a press.
        ESP_LOGI(TAG, "Closed endstop released.");
        if (this->current_operation == COVER_OPERATION_IDLE) {
          this->target_position_ = COVER_OPEN;
          this->start_direction_(COVER_OPERATION_OPENING, false);
        }
      }
    });
  }
}

void GdoCover::loop() {
  if ((this->pending_stop_ || this->pending_position_.has_value()) && !this->press_in_progress_()) {
    const bool stop = this->pending_stop_;
    const optional<float> position = this->pending_position_;
    this->pending_stop_ = false;
    this->pending_position_.reset();
    ESP_LOGI(TAG, "Press finished. Running the command that was held.");
    this->perform_control_(stop, position);
  }

  if (this->current_operation == COVER_OPERATION_IDLE) {
    return;
  }

  const uint32_t now = millis();

  // Recompute position every loop cycle
  this->recompute_position_();

  // Waiting on the rest of a multi-press: stopping now would cancel the presses
  // that have not happened yet.
  if (this->is_at_target_() && !this->press_in_progress_()) {
    if (this->target_position_ == COVER_OPEN || this->target_position_ == COVER_CLOSED) {
      // Don't trigger stop, let the cover stop by itself.
      this->current_operation = COVER_OPERATION_IDLE;
    } else {
      this->start_direction_(COVER_OPERATION_IDLE);
    }
    this->publish_state();
  } else if ((this->current_operation == COVER_OPERATION_OPENING && this->open_endstop_ != nullptr &&
              now - this->start_dir_time_ > endstop_timeout(this->open_duration_)) ||
             (this->current_operation == COVER_OPERATION_CLOSING && this->close_endstop_ != nullptr &&
              now - this->start_dir_time_ > endstop_timeout(this->close_duration_))) {
    ESP_LOGI(TAG, "Failed to reach endstop. Likely stopped externally.");
    this->position = UNKNOWN_POSITION;
    this->current_operation = COVER_OPERATION_IDLE;
    this->publish_state();
  }

  // Send current position every second
  if (now - this->last_publish_time_ > 1000) {
    this->publish_state(false);
    this->last_publish_time_ = now;
  }
}

float GdoCover::get_setup_priority() const { return setup_priority::DATA; }

CoverTraits GdoCover::get_traits() {
  auto traits = CoverTraits();
  traits.set_supports_stop(true);
  traits.set_supports_position(true);
  return traits;
}

void GdoCover::control(const CoverCall &call) {
  if (this->press_in_progress_()) {
    // Pressing now would put an impulse on the wire too soon after the ones
    // this press has already sent for the opener to tell them apart, and
    // cancelling the rest of the press would leave it half done. Hold the
    // command until the relay is finished.
    ESP_LOGI(TAG, "A press is still in progress. Holding the command until it finishes.");
    this->pending_stop_ = call.get_stop();
    this->pending_position_ = call.get_position();
    return;
  }
  this->perform_control_(call.get_stop(), call.get_position());
}

void GdoCover::perform_control_(bool stop, const optional<float> &position) {
  if (stop) {
    this->start_direction_(COVER_OPERATION_IDLE);
    this->publish_state();
  }
  if (position.has_value()) {
    auto pos = *position;
    if (pos == this->position) {
      ESP_LOGI(TAG, "Nothing to do. Already at target position.");
    } else {
      auto op = pos < this->position ? COVER_OPERATION_CLOSING : COVER_OPERATION_OPENING;
      const float prev_target = this->target_position_;
      this->target_position_ = pos;
      if (!this->start_direction_(op)) {
        // The door kept doing whatever it was doing, so the target it was
        // travelling to has to stay as it was or loop() would cut it short.
        this->target_position_ = prev_target;
      }
    }
  }
}

// A multi-press action leaves the door standing where it is until its last
// press: the earlier ones only stop or reverse it. While one is still running
// the door is not yet travelling the way it was asked to.
bool GdoCover::press_in_progress_() {
  return this->prev_command_trigger_ != nullptr && this->prev_command_trigger_->is_action_running();
}

void GdoCover::stop_prev_trigger_() {
  if (this->prev_command_trigger_ != nullptr) {
    this->prev_command_trigger_->stop_action();
    this->prev_command_trigger_ = nullptr;
  }
}

bool GdoCover::is_at_target_() const {
  switch (this->current_operation) {
    case COVER_OPERATION_OPENING:
      if (this->target_position_ == COVER_OPEN && this->open_endstop_ != nullptr) {
        return this->open_endstop_->state;
      }
      return this->position >= this->target_position_;
    case COVER_OPERATION_CLOSING:
      if (this->target_position_ == COVER_CLOSED && this->close_endstop_ != nullptr) {
        return this->close_endstop_->state;
      }
      return this->position <= this->target_position_;
    case COVER_OPERATION_IDLE:
    default:
      return true;
  }
}

// The opener has no way to tell us what it is doing, so model it: given what
// the door is doing now, what will it be doing after one press of the button?
// Returns nothing when that cannot be told, which only happens on an opener
// that goes against the last direction of travel when there has not been one.
optional<CoverOperation> GdoCover::op_after_press_(CoverOperation op, float position,
                                                   CoverOperation last_travel_dir) const {
  switch (op) {
    case COVER_OPERATION_OPENING:
      return COVER_OPERATION_IDLE;
    case COVER_OPERATION_CLOSING:
      if (this->press_while_closing_ == PRESS_WHILE_CLOSING_STOPS) {
        return COVER_OPERATION_IDLE;
      }
      return COVER_OPERATION_OPENING;
    case COVER_OPERATION_IDLE:
    default:
      break;
  }
  // A door standing on an endstop can only travel one way.
  if (position == COVER_CLOSED) {
    return COVER_OPERATION_OPENING;
  }
  if (position == COVER_OPEN) {
    return COVER_OPERATION_CLOSING;
  }
  if (this->press_while_closing_ == PRESS_WHILE_CLOSING_OPENS) {
    return COVER_OPERATION_CLOSING;
  }
  switch (last_travel_dir) {
    case COVER_OPERATION_OPENING:
      return COVER_OPERATION_CLOSING;
    case COVER_OPERATION_CLOSING:
      return COVER_OPERATION_OPENING;
    default:
      return {};
  }
}

// Returns false if the opener cannot get from what it is doing now to dir, so
// that the caller can leave the current travel and its target alone.
bool GdoCover::start_direction_(CoverOperation dir, bool perform_trigger) {
  if (dir == this->current_operation) {
    ESP_LOGI(TAG, "Nothing to do. CoverOperation %d didn't change.", dir);
    return true;
  }

  this->recompute_position_();

  if (this->current_operation == COVER_OPERATION_IDLE) {
    if (dir == COVER_OPERATION_OPENING && this->position == COVER_OPEN) {
      ESP_LOGW(TAG, "Door is fully open. Cannot open more.");
      return false;
    }
    if (dir == COVER_OPERATION_CLOSING && this->position == COVER_CLOSED) {
      ESP_LOGW(TAG, "Door is fully closed. Cannot close more.");
      return false;
    }
  }

  // Someone else moved the door: there is nothing to press, only state to catch
  // up with.
  Trigger<> *trig = nullptr;
  if (perform_trigger) {
    Trigger<> *const press_triggers[] = {&this->single_press_trigger_, &this->double_press_trigger_,
                                         &this->triple_press_trigger_};
    static const char *const PRESS_NAMES[] = {"single", "double", "triple"};
    const char *presses = nullptr;
    // Press the button in the model until the door ends up doing what was
    // asked. Two presses are enough on an opener that reverses a closing door;
    // one that stops it needs three to send a door that was stopped part-way
    // back the way it came.
    CoverOperation op = this->current_operation;
    float position = this->position;
    CoverOperation last_travel_dir = this->last_travel_dir_;
    for (size_t i = 0; i < sizeof(PRESS_NAMES) / sizeof(PRESS_NAMES[0]); i++) {
      const optional<CoverOperation> next_op = this->op_after_press_(op, position, last_travel_dir);
      if (!next_op.has_value()) {
        break;
      }
      op = *next_op;
      if (op != COVER_OPERATION_IDLE) {
        // The door is travelling again, so it is no longer on an endstop.
        last_travel_dir = op;
        position = UNKNOWN_POSITION;
      }
      if (op == dir) {
        trig = press_triggers[i];
        presses = PRESS_NAMES[i];
        break;
      }
    }
    if (trig == nullptr) {
      if (this->last_travel_dir_ == COVER_OPERATION_IDLE) {
        // Nothing has moved the door since boot, so which way this opener sends
        // a door that is standing part-way is anybody's guess. Once the door
        // reaches an endstop the next press is predictable again.
        ESP_LOGW(TAG, "Door is at position %.2f and has not moved since boot, so the next press is unpredictable.",
                 this->position);
      } else {
        ESP_LOGW(TAG, "Door is %s at position %.2f. Cannot make it %s with this opener.",
                 LOG_STR_ARG(cover_operation_to_str(this->current_operation)), this->position,
                 LOG_STR_ARG(cover_operation_to_str(dir)));
      }
      return false;
    }
    ESP_LOGI(TAG, "Door is %s at position %.2f. Asked to make it %s. Performing a %s press.",
             LOG_STR_ARG(cover_operation_to_str(this->current_operation)), this->position,
             LOG_STR_ARG(cover_operation_to_str(dir)), presses);
  }

  this->current_operation = dir;
  if (dir != COVER_OPERATION_IDLE) {
    this->last_travel_dir_ = dir;
  }

  const uint32_t now = millis();
  this->start_dir_time_ = now;
  this->last_recompute_time_ = now;

  if (trig != nullptr) {
    this->stop_prev_trigger_();
    trig->trigger();
    this->prev_command_trigger_ = trig;
  }
  return true;
}

void GdoCover::recompute_position_() {
  float dir;
  float action_dur;
  float min_pos = COVER_CLOSED;
  float max_pos = COVER_OPEN;
  switch (this->current_operation) {
    case COVER_OPERATION_OPENING:
      dir = 1.0f;
      action_dur = this->open_duration_;
      // Only claim fully open once the endstop says so. Gate on the target
      // being COVER_OPEN, so a partial target of e.g. 0.995 stays reachable.
      if (this->open_endstop_ != nullptr && this->target_position_ == COVER_OPEN && !this->open_endstop_->state) {
        max_pos = ALMOST_OPEN;
      }
      break;
    case COVER_OPERATION_CLOSING:
      dir = -1.0f;
      action_dur = this->close_duration_;
      if (this->close_endstop_ != nullptr && this->target_position_ == COVER_CLOSED && !this->close_endstop_->state) {
        min_pos = ALMOST_CLOSED;
      }
      break;
    case COVER_OPERATION_IDLE:
    default:
      return;
  }
  const uint32_t now = millis();
  if (this->press_in_progress_()) {
    // Credit no travel for this tick, but keep the clock moving so that the
    // time the door stood still is not counted once it does start.
    this->last_recompute_time_ = now;
    return;
  }
  this->position += dir * (now - this->last_recompute_time_) / action_dur;
  this->position = clamp(this->position, min_pos, max_pos);
  this->last_recompute_time_ = now;
}

}  // namespace gdo
}  // namespace esphome
