#include "readings.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace aecc {

static const char *const TAG = "aecc.sensor";
static const char *const BS_TAG = "aecc.binary_sensor";

void AeccSensor::setup() {
  if (this->parent_ == nullptr) {
    this->mark_failed();
    return;
  }
  if (this->metric_ == Metric::REGISTER) {
    this->parent_->add_watch(this->address_, this->interval_);
  } else if (this->parent_->control() == nullptr) {
    ESP_LOGE(TAG, "control diagnostic configured but control: is not");
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


void AeccBinarySensor::setup() {
  if (this->health_ == Health::EMS_READY) {
    if (this->parent_ == nullptr || !this->parent_->datalogger_present()) {
      ESP_LOGE(BS_TAG, "ems_ready needs a datalogger to be configured");
      this->mark_failed();
    }
    return;
  }
  if (this->parent_ == nullptr || this->parent_->control() == nullptr) {
    ESP_LOGE(BS_TAG, "needs control: to be configured");
    this->mark_failed();
  }
}

void AeccBinarySensor::loop() {
  if (this->health_ == Health::EMS_READY) {
    this->publish_state(this->parent_->ems_ready());
    return;
  }
  auto *c = this->parent_->control();
  switch (this->health_) {
    case Health::CONTROL_EFFECTIVE:
      this->publish_state(c->effective());
      break;
    case Health::METER_OK:
      this->publish_state(c->meter_ok());
      break;
    case Health::EMS_READY:
      this->publish_state(this->parent_->ems_ready());
      break;
  }
}

void AeccBinarySensor::dump_config() { LOG_BINARY_SENSOR("", "AECC health", this); }

}  // namespace aecc
}  // namespace esphome
