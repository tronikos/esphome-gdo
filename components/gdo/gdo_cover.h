#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/optional.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/cover/cover.h"

namespace esphome {
namespace gdo {

// What the opener does when the button is pressed while the door is closing.
// The rest of the button is the same on every opener: a press starts a closed
// door opening, starts a fully open door closing, and stops an opening one.
enum PressWhileClosing : uint8_t {
  // The door reverses and starts opening. Such an opener never leaves the door
  // standing part-way by itself, so it treats any door off the open endstop as
  // open and closes it on the next press.
  PRESS_WHILE_CLOSING_OPENS = 0,
  // The door stops, mirroring what a press does while the door is opening.
  // These openers run an impulse sequence (open - stop - close - stop - open),
  // so a press on a door standing part-way travels against the direction the
  // door last moved in.
  PRESS_WHILE_CLOSING_STOPS = 1,
};

class GdoCover : public cover::Cover, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  Trigger<> *get_single_press_trigger() { return &this->single_press_trigger_; }
  Trigger<> *get_double_press_trigger() { return &this->double_press_trigger_; }
  Trigger<> *get_triple_press_trigger() { return &this->triple_press_trigger_; }
  void set_open_endstop(binary_sensor::BinarySensor *open_endstop) { this->open_endstop_ = open_endstop; }
  void set_close_endstop(binary_sensor::BinarySensor *close_endstop) { this->close_endstop_ = close_endstop; }
  void set_open_duration(uint32_t open_duration) { this->open_duration_ = open_duration; }
  void set_close_duration(uint32_t close_duration) { this->close_duration_ = close_duration; }
  void set_press_while_closing(PressWhileClosing press_while_closing) {
    this->press_while_closing_ = press_while_closing;
  }

  cover::CoverTraits get_traits() override;

 protected:
  void control(const cover::CoverCall &call) override;
  void perform_control_(bool stop, const optional<float> &position);
  void stop_prev_trigger_();
  bool press_in_progress_();
  bool is_at_target_() const;

  optional<cover::CoverOperation> op_after_press_(cover::CoverOperation op, float position,
                                                  cover::CoverOperation last_travel_dir) const;
  bool start_direction_(cover::CoverOperation dir, bool perform_trigger = true);

  void recompute_position_();

  binary_sensor::BinarySensor *open_endstop_{nullptr};
  binary_sensor::BinarySensor *close_endstop_{nullptr};
  uint32_t open_duration_{0};
  uint32_t close_duration_{0};
  PressWhileClosing press_while_closing_{PRESS_WHILE_CLOSING_OPENS};
  Trigger<> single_press_trigger_;
  Trigger<> double_press_trigger_;
  Trigger<> triple_press_trigger_;
  Trigger<> *prev_command_trigger_{nullptr};
  // The direction the door last travelled in, which is what an opener with
  // PRESS_WHILE_CLOSING_STOPS goes against on the next press. IDLE means it has
  // not travelled since boot, so the next press cannot be predicted.
  cover::CoverOperation last_travel_dir_{cover::COVER_OPERATION_IDLE};
  // A command that arrived while the relay was still working through a press,
  // held back until it can be pressed out cleanly. The newest one wins.
  bool pending_stop_{false};
  optional<float> pending_position_{};
  uint32_t last_recompute_time_{0};
  uint32_t start_dir_time_{0};
  uint32_t last_publish_time_{0};
  float target_position_{0};
};

}  // namespace gdo
}  // namespace esphome
