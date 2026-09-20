#include "aecc_sensor.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cmath>

namespace esphome {
namespace aecc {

static const char *const TAG = "aecc.sensor";

void AeccSensor::setup() {
  if (this->parent_ == nullptr) {
    this->mark_failed();
    return;
  }
  if (this->metric_ == Metric::REGISTER) {
    this->parent_->add_watch(this->address_, this->interval_);
  } else if (this->parent_->control() == nullptr) {
    ESP_LOGE(TAG, "control diagnostic configured but zero_export is not enabled");
    this->mark_failed();
  }
}

void AeccSensor::publish_metric_() {
  auto *c = this->parent_->control();
  const uint32_t now = millis();
  if (now - this->published_at_ < this->interval_)
    return;
  this->published_at_ = now;

  switch (this->metric_) {
    case Metric::GRID_W:
      this->publish_state(c->last_grid_w());
      break;
    case Metric::GRID_FILTERED_W:
      this->publish_state(c->filtered_grid_w());
      break;
    case Metric::METER_AGE_S: {
      auto *m = c->meter();
      this->publish_state(m == nullptr || !m->ever_read() ? NAN : m->age_ms() / 1000.0f);
      break;
    }
    case Metric::COMMAND_W:
      this->publish_state((float) c->command());
      break;
    case Metric::LOOP_HZ: {
      const uint32_t loops = c->loops();
      if (this->rate_at_ != 0 && now > this->rate_at_)
        this->publish_state((loops - this->rate_loops_) * 1000.0f / (now - this->rate_at_));
      this->rate_at_ = now;
      this->rate_loops_ = loops;
      break;
    }
    case Metric::REGISTER:
      break;
  }
}

void AeccSensor::loop() {
  if (this->metric_ != Metric::REGISTER) {
    this->publish_metric_();
    return;
  }

  uint16_t raw;
  uint32_t stamped;
  if (!this->parent_->get_register(this->address_, &raw, &stamped))
    return;

  if (this->have_published_ && stamped == this->published_at_)
    return;
  this->published_at_ = stamped;
  this->have_published_ = true;

  const float value = this->signed_ ? (float) (int16_t) raw : (float) raw;
  this->publish_state(value * this->scale_);
}

void AeccSensor::dump_config() {
  LOG_SENSOR("", "AECC sensor", this);
  ESP_LOGCONFIG(TAG, "  Register: 0x%04X", this->address_);
}

}  // namespace aecc
}  // namespace esphome
