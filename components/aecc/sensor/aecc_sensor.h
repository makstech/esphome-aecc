#pragma once

#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

#include "../aecc_component.h"

namespace esphome {
namespace aecc {

/// Most sensors are a register; the rest expose what the control loop itself is doing,
/// which is not readable from the inverter.
enum class Metric : uint8_t {
  REGISTER = 0,
  GRID_W,
  GRID_FILTERED_W,
  METER_AGE_S,
  COMMAND_W,
  LOOP_HZ,
};

class AeccSensor : public sensor::Sensor, public Component, public AeccDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_address(uint16_t address) { this->address_ = address; }
  void set_signed(bool is_signed) { this->signed_ = is_signed; }
  void set_scale(float scale) { this->scale_ = scale; }
  void set_interval(uint32_t interval_ms) { this->interval_ = interval_ms; }
  void set_metric(Metric metric) { this->metric_ = metric; }

 protected:
  void publish_metric_();

  Metric metric_{Metric::REGISTER};
  uint16_t address_{0};
  bool signed_{false};
  float scale_{1.0f};
  uint32_t interval_{5000};
  uint32_t published_at_{0};
  bool have_published_{false};
  uint32_t rate_at_{0};
  uint32_t rate_loops_{0};
};

}  // namespace aecc
}  // namespace esphome
