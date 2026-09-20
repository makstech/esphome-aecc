#pragma once

#include <map>
#include <string>
#include <vector>

#include <cstdint>

namespace esphome {
namespace aecc {

/// Newline-delimited JSON over TCP 8080 on the unit's built-in WiFi module.
///
/// This is the only way to reach the EMS registers: they are not Modbus-addressable, and
/// they hold the schedule slot that decides whether 0xFE16 does anything at all. Blocking,
/// so it runs from the bus task.
///
/// Serves one client at a time — a running Home Assistant integration pointed at the same
/// unit will starve it. A malformed request gets silence rather than an error, which looks
/// identical to that starvation.
class Datalogger {
 public:
  void set_host(const std::string &host) { this->host_ = host; }
  void set_port(uint16_t port) { this->port_ = port; }
  bool configured() const { return !this->host_.empty(); }
  bool reachable() const { return this->reachable_; }

  bool read(const std::vector<uint16_t> &addrs, std::map<uint16_t, std::string> &out);
  bool write(const std::map<uint16_t, std::string> &values);

  /// The resting state a dead controller leaves behind. Negative charges, and charging
  /// cannot export at any load or state of charge.
  static std::string slot(int32_t watts, uint8_t max_soc, uint8_t min_soc);

 protected:
  /// Reconnects per call: holding the socket would lock everything else out of the single
  /// client slot, and per-call connection was measured to work for writes as well as reads.
  bool call_(const std::string &request, std::string &reply);

  std::string host_;
  uint16_t port_{8080};
  uint32_t serial_{0};
  bool reachable_{false};
};

}  // namespace aecc
}  // namespace esphome
