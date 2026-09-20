#include "meter.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cmath>

namespace esphome {
namespace aecc {

static const char *const TAG = "aecc.meter";

bool Rs071Meter::poll(float *watts) {
  float value;
  // A retry inside the tick costs the whole loop period; the next tick is the retry.
  if (!this->bus_.read_f32(this->address_, &value, 1)) {
    this->mark_fail();
    return false;
  }
  if (!std::isfinite(value)) {
    ESP_LOGW(TAG, "discarding non-finite reading");
    this->mark_fail();
    return false;
  }
  *watts = value;
  this->mark_ok();
  return true;
}

bool Rs071Meter::confirms_meter() {
  float volts;
  return this->bus_.read_f32(meter_reg::VOLTAGE, &volts, 2) && volts > 150.0f && volts < 300.0f;
}

bool Rs071Meter::looks_like_inverter() {
  uint16_t soc;
  return this->bus_.read_one(reg::SOC, &soc, 2) && soc <= 100;
}

}  // namespace aecc
}  // namespace esphome
