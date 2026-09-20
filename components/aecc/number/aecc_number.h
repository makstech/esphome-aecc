#pragma once

#include "esphome/components/number/number.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include "../aecc_component.h"

namespace esphome {
namespace aecc {

enum class ControlParam : uint8_t {
  NONE = 0,
  GRID_TARGET,
  MAX_DISCHARGE,
  MAX_CHARGE,
};

/// Either a controller parameter or an inverter register, and the difference decides
/// whether the value persists to flash.
///
/// The inverter owns its own settings, so a register-backed number never restores: the
/// unit is read at boot and is authoritative. Persisting one would create a second
/// source of truth that silently overwrites a change made in the vendor app at the next
/// reboot. Controller parameters exist nowhere else, so those must persist or a reboot
/// discards the optimiser's last instruction.
class AeccNumber : public number::Number, public Component, public AeccDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_control_param(ControlParam param) { this->param_ = param; }
  void set_address(uint16_t address) { this->address_ = address; }
  void set_scale(float scale) { this->scale_ = scale; }
  void set_interval(uint32_t interval_ms) { this->interval_ = interval_ms; }

 protected:
  void control(float value) override;
  void apply_(float value);
  float current_();

  ControlParam param_{ControlParam::NONE};
  uint16_t address_{0};
  float scale_{1.0f};
  uint32_t interval_{30000};
  uint32_t published_at_{0};
  bool have_published_{false};
  ESPPreferenceObject pref_;
};

}  // namespace aecc
}  // namespace esphome
