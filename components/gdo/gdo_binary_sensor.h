#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"

namespace esphome {
namespace gdo {

struct ObstructionStore {
  // Incremented from the ISR, read and cleared from loop(). volatile stops the
  // compiler from caching it in a register across the loop() body.
  volatile uint16_t obstruction_low_count = 0;  // count obstruction low pulses

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
  uint32_t last_check_time_{0};
  uint32_t last_asleep_time_{0};
};

}  // namespace gdo
}  // namespace esphome
