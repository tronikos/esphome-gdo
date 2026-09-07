#include "gdo_binary_sensor.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

namespace esphome {
namespace gdo {

static const char *const TAG = "gdo.binary_sensor";

// How often the pulse counter is sampled.
static const uint32_t CHECK_PERIOD = 50;
// More than this many pulses within CHECK_PERIOD means the line is awake and clear.
static const uint16_t PULSES_LOWER_LIMIT = 3;
// How long the line has to be steady high, after the last time it was seen low,
// before that counts as an obstruction rather than the wake-up ramp.
static const uint32_t AWAKE_SETTLE_TIME = 700;

void IRAM_ATTR HOT ObstructionStore::s_gpio_intr(ObstructionStore *store) { store->obstruction_low_count++; }

void GdoBinarySensor::setup() {
  this->input_obst_pin_->setup();
  this->input_obst_pin_->pin_mode(gpio::FLAG_INPUT);
  this->input_obst_pin_->attach_interrupt(&ObstructionStore::s_gpio_intr, &this->isr_store_,
                                          gpio::INTERRUPT_FALLING_EDGE);

  const uint32_t now = millis();
  this->last_check_time_ = now;
  // Start the settle timer now rather than at 0, so that booting with the line
  // already high does not immediately look like an obstruction.
  this->last_asleep_time_ = now;
  // Without this the entity stays unknown for as long as the opener is asleep,
  // which can be indefinitely.
  this->publish_initial_state(false);
}

void GdoBinarySensor::loop() {
  // The obstruction sensor has 3 states: clear (HIGH with a LOW pulse every 7ms),
  // obstructed (HIGH), asleep (LOW). The transitions between awake and asleep are
  // tricky because the voltage drops slowly when falling asleep and is high
  // without pulses when waking up.
  const uint32_t now = millis();
  if (now - this->last_check_time_ <= CHECK_PERIOD) {
    return;
  }
  this->last_check_time_ = now;

  // Read and clear the counter as one operation, so a pulse arriving between the
  // read and the reset is not silently dropped.
  uint16_t pulses;
  {
    InterruptLock lock;
    pulses = this->isr_store_.obstruction_low_count;
    this->isr_store_.obstruction_low_count = 0;
  }

  if (pulses > PULSES_LOWER_LIMIT) {
    // Awake and pulsing, so not obstructed. Nothing else needs checking.
    this->publish_state(false);
  } else if (pulses == 0) {
    // No pulses at all, so the line is steady high or steady low.
    if (!this->input_obst_pin_->digital_read()) {
      this->last_asleep_time_ = now;
    } else if (now - this->last_asleep_time_ > AWAKE_SETTLE_TIME) {
      this->publish_state(true);
    }
  }
}

void GdoBinarySensor::dump_config() {
  LOG_BINARY_SENSOR("", "GDO Obstruction", this);
  LOG_PIN("  Obstruction Pin: ", this->input_obst_pin_);
}

}  // namespace gdo
}  // namespace esphome
