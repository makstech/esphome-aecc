#pragma once

#include <map>
#include <string>
#include <vector>

#include <cstdint>

namespace esphome {
namespace aecc {

/// What the battery's own scheduler is doing. The values are the register's own, so a
/// reading maps straight onto the enum.
enum class WorkMode : uint8_t {
  SELF_CONSUMPTION = 3,
  CUSTOM = 6,
};

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
  /// An address, a hostname, or an mDNS name. Changing it drops the cached address.
  void set_host(const std::string &host) {
    if (host == this->host_)
      return;
    this->host_ = host;
    this->resolved_ = 0;
    this->resolved_at_ = 0;
    this->close_();
  }
  void set_port(uint16_t port) { this->port_ = port; }
  void set_enabled(bool enabled) { this->enabled_ = enabled; }
  /// A datalogger exists in the configuration. Separate from configured(), because
  /// switching it off must not read as "this unit has no scheduler to reconcile".
  bool present() const { return !this->host_.empty(); }
  bool configured() const { return this->enabled_ && this->present(); }
  bool reachable() const { return this->reachable_; }

  bool read(const std::vector<uint16_t> &addrs, std::map<uint16_t, std::string> &out);
  /// Closes a connection once it has sat idle, or at once while switched off, so the
  /// single client slot is free for the vendor app or integration. Bus task only.
  void release_idle();
  bool write(const std::map<uint16_t, std::string> &values);

  /// The resting state a dead controller leaves behind. Negative charges, and charging
  /// cannot export at any load or state of charge.
  static std::string slot(int32_t watts, uint8_t max_soc, uint8_t min_soc);

 protected:
  /// Reuses the socket for calls that follow each other closely: the datalogger ignores a
  /// connection opened just after the previous one closed, so a read then a write on fresh
  /// sockets loses the write.
  bool call_(const std::string &request, std::string &reply);
  bool connect_();
  void close_();
  /// The host as an IPv4 address, resolving it if it is a name. Cached, because every
  /// call would otherwise pay for a lookup, and re-resolved only after a failure so a
  /// battery that moves on DHCP is still found.
  bool resolve_(uint32_t *addr);

  std::string host_;
  uint16_t port_{8080};
  uint32_t serial_{0};
  bool reachable_{false};
  volatile bool enabled_{true};
  uint32_t resolved_{0};
  uint32_t resolved_at_{0};
  int fd_{-1};
  uint32_t used_at_{0};
};

}  // namespace aecc
}  // namespace esphome
