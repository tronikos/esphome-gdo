#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

namespace esphome {
namespace gdo {

struct ObstructionStore {
  int obstruction_low_count = 0;  // count obstruction low pulses

  static void s_gpio_intr(ObstructionStore *store);
};

class GdoBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  void set_input_obst_pin(InternalGPIOPin *pin) { this->input_obst_pin_ = pin; }

 protected:
  InternalGPIOPin *input_obst_pin_{nullptr};
  ObstructionStore isr_store_{};
};

}  // namespace gdo
}  // namespace esphome
