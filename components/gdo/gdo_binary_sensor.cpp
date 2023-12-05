#include "gdo_binary_sensor.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace gdo {

static const char *const TAG = "gdo.binary_sensor";

void IRAM_ATTR HOT ObstructionStore::s_gpio_intr(ObstructionStore *store) { store->obstruction_low_count++; }

void GdoBinarySensor::setup() {
  this->input_obst_pin_->setup();
  this->input_obst_pin_->pin_mode(gpio::FLAG_INPUT);
  this->input_obst_pin_->attach_interrupt(&ObstructionStore::s_gpio_intr, &this->isr_store_,
                                          gpio::INTERRUPT_FALLING_EDGE);
}

void GdoBinarySensor::loop() {
  long current_millis = millis();
  static unsigned long last_millis = 0;
  static unsigned long last_asleep = 0;

  // the obstruction sensor has 3 states: clear (HIGH with LOW pulse every 7ms), obstructed (HIGH), asleep (LOW)
  // the transitions between awake and asleep are tricky because the voltage drops slowly when falling asleep
  // and is high without pulses when waking up

  // If at least 3 low pulses are counted within 50ms, the door is awake, not obstructed and we don't have to check
  // anything else

  const long CHECK_PERIOD = 50;
  const long PULSES_LOWER_LIMIT = 3;

  if (current_millis - last_millis > CHECK_PERIOD) {
    // check to see if we got more then PULSES_LOWER_LIMIT pulses
    if (this->isr_store_.obstruction_low_count > PULSES_LOWER_LIMIT) {
      this->publish_state(false);
    } else if (this->isr_store_.obstruction_low_count == 0) {
      // if there have been no pulses the line is steady high or low
      if (!this->input_obst_pin_->digital_read()) {
        // asleep
        last_asleep = current_millis;
      } else {
        // if the line is high and was last asleep more than 700ms ago, then there is an obstruction present
        if (current_millis - last_asleep > 700) {
          this->publish_state(true);
        }
      }
    }
    last_millis = current_millis;
    this->isr_store_.obstruction_low_count = 0;
  }
}

void GdoBinarySensor::dump_config() {
  LOG_BINARY_SENSOR("", "GDO Binary Sensor", this);
  LOG_PIN("  Obstruction Pin: ", this->input_obst_pin_);
}

}  // namespace gdo
}  // namespace esphome
