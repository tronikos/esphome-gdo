#include "gdo_cover.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace gdo {

static const char *const TAG = "gdo.cover";

const float UNKNOWN_POSITION = 0.5f;

using namespace esphome::cover;

void GdoCover::dump_config() {
  LOG_COVER("", "Time Based Endstop Cover", this);
  LOG_BINARY_SENSOR("  ", "Open Endstop", this->open_endstop_);
  ESP_LOGCONFIG(TAG, "  Open Duration: %.1fs", this->open_duration_ / 1e3f);
  LOG_BINARY_SENSOR("  ", "Close Endstop", this->close_endstop_);
  ESP_LOGCONFIG(TAG, "  Close Duration: %.1fs", this->close_duration_ / 1e3f);
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
  if (this->current_operation == COVER_OPERATION_IDLE) {
    return;
  }

  const uint32_t now = millis();

  // Recompute position every loop cycle
  this->recompute_position_();

  if (this->is_at_target_()) {
    if (this->target_position_ == COVER_OPEN || this->target_position_ == COVER_CLOSED) {
      // Don't trigger stop, let the cover stop by itself.
      this->current_operation = COVER_OPERATION_IDLE;
    } else {
      this->start_direction_(COVER_OPERATION_IDLE);
    }
    this->publish_state();
  } else if ((this->current_operation == COVER_OPERATION_OPENING && this->open_endstop_ != nullptr &&
              now - this->start_dir_time_ > this->open_duration_) ||
             (this->current_operation == COVER_OPERATION_CLOSING && this->close_endstop_ != nullptr &&
              now - this->start_dir_time_ > this->close_duration_)) {
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
  if (call.get_stop()) {
    this->start_direction_(COVER_OPERATION_IDLE);
    this->publish_state();
  }
  if (call.get_position().has_value()) {
    auto pos = *call.get_position();
    if (pos == this->position) {
      ESP_LOGI(TAG, "Nothing to do. Already at target position.");
    } else {
      auto op = pos < this->position ? COVER_OPERATION_CLOSING : COVER_OPERATION_OPENING;
      this->target_position_ = pos;
      this->start_direction_(op);
    }
  }
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

void GdoCover::start_direction_(CoverOperation dir, bool perform_trigger) {
  if (dir == this->current_operation) {
    ESP_LOGI(TAG, "Nothing to do. CoverOperation %d didn't change.", dir);
    return;
  }

  this->recompute_position_();
  Trigger<> *trig;
  switch (dir) {
    case COVER_OPERATION_IDLE:
      switch (this->current_operation) {
        case COVER_OPERATION_OPENING:
          ESP_LOGI(TAG, "Door is opening. Asked to stop.");
          trig = this->single_press_trigger_;
          break;
        case COVER_OPERATION_CLOSING:
          ESP_LOGI(TAG, "Door is closing. Asked to stop.");
          trig = this->double_press_trigger_;
          break;
        default:
          return;
      }
      break;
    case COVER_OPERATION_OPENING:
      switch (this->current_operation) {
        case COVER_OPERATION_IDLE:
          if (this->position == COVER_CLOSED) {
            ESP_LOGI(TAG, "Door is fully closed. Asked to open.");
            trig = this->single_press_trigger_;
          } else if (this->position == COVER_OPEN) {
            ESP_LOGW(TAG, "Door is fully open. Cannot open more.");
            return;
          } else {
            ESP_LOGI(TAG, "Door is partially open. Asked to open more.");
            trig = this->double_press_trigger_;
          }
          break;
        case COVER_OPERATION_CLOSING:
          ESP_LOGI(TAG, "Door is closing. Asked to open.");
          trig = this->single_press_trigger_;
          break;
        default:
          return;
      }
      break;
    case COVER_OPERATION_CLOSING:
      switch (this->current_operation) {
        case COVER_OPERATION_IDLE:
          if (this->position == COVER_CLOSED) {
            ESP_LOGI(TAG, "Door is fully closed. Cannot close more.");
            return;
          } else if (this->position == COVER_OPEN) {
            ESP_LOGW(TAG, "Door is fully open. Asked to close.");
            trig = this->single_press_trigger_;
          } else {
            ESP_LOGI(TAG, "Door is partially open. Asked to close more.");
            trig = this->single_press_trigger_;
          }
          break;
        case COVER_OPERATION_OPENING:
          ESP_LOGI(TAG, "Door is opening. Asked to close.");
          trig = this->double_press_trigger_;
          break;
        default:
          return;
      }
      break;
    default:
      return;
  }

  this->current_operation = dir;

  const uint32_t now = millis();
  this->start_dir_time_ = now;
  this->last_recompute_time_ = now;

  if (perform_trigger) {
    this->stop_prev_trigger_();
    trig->trigger();
    this->prev_command_trigger_ = trig;
  }
}

void GdoCover::recompute_position_() {
  float dir;
  float action_dur;
  switch (this->current_operation) {
    case COVER_OPERATION_OPENING:
      dir = 1.0f;
      action_dur = this->open_duration_;
      break;
    case COVER_OPERATION_CLOSING:
      dir = -1.0f;
      action_dur = this->close_duration_;
      break;
    case COVER_OPERATION_IDLE:
    default:
      return;
  }
  const uint32_t now = millis();
  this->position += dir * (now - this->last_recompute_time_) / action_dur;
  this->position = clamp(this->position, 0.0f, 1.0f);
  this->last_recompute_time_ = now;
}

}  // namespace gdo
}  // namespace esphome
