#pragma once

#include "esphome/core/automation.h"

#include "aecc_component.h"

namespace esphome {
namespace aecc {

/// Write any register, whether or not it is modelled as an entity.
///
/// The map in registers.h is what has been identified, not what exists. Without this,
/// a setting nobody has named yet would be unreachable — and this component is the only
/// way to reach the hardware once the vendor's servers are gone.
template<typename... Ts> class WriteRegisterAction : public Action<Ts...> {
 public:
  explicit WriteRegisterAction(AeccComponent *parent) : parent_(parent) {}

  TEMPLATABLE_VALUE(uint16_t, address)
  TEMPLATABLE_VALUE(uint16_t, value)

  void play(Ts... x) override {
    this->parent_->queue_write(this->address_.value(x...), this->value_.value(x...));
  }

 protected:
  AeccComponent *parent_;
};

}  // namespace aecc
}  // namespace esphome
