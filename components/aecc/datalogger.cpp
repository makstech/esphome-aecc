#include "datalogger.h"

#include <algorithm>
#include <cmath>
#include "esphome/components/json/json_util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
#include <netdb.h>
#ifdef USE_ESP32
#include <esp_task_wdt.h>
#endif
#ifdef USE_MDNS
#include <mdns.h>
#endif
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace esphome {
namespace aecc {

static const char *const TAG = "aecc.datalogger";

// This runs on the bus task, which is subscribed to the 5 s task watchdog with panic
// enabled. Every wait below is bounded so the whole call cannot approach that: an
// unreachable datalogger, or one whose single client slot is held by something else,
// must degrade to a failed call rather than a reboot.
static const uint32_t CONNECT_MS = 1200;
// mDNS answers on the local segment or not at all, so a short window is enough and keeps
// the lookup well inside the task watchdog.
static const uint32_t MDNS_MS = 1500;
// Long enough that a working address is not re-looked-up, short enough that a battery
// which moved on DHCP is found again without a reboot.
static const uint32_t RESOLVE_TTL_MS = 300000;
static const uint32_t RECV_MS = 1200;
static const uint32_t TOTAL_MS = 3000;

std::string Datalogger::slot(int32_t watts, uint8_t max_soc, uint8_t min_soc) {
  char buf[64];
  snprintf(buf, sizeof(buf), "1,00:00,23:59,%d,0,6,0,0,0,%u,%u", (int) watts, (unsigned) max_soc,
           (unsigned) min_soc);
  return buf;
}

bool Datalogger::resolve_(uint32_t *addr) {
  struct in_addr literal {};
  if (::inet_pton(AF_INET, this->host_.c_str(), &literal) == 1) {
    *addr = literal.s_addr;
    return true;
  }

  if (this->resolved_ != 0 && millis() - this->resolved_at_ < RESOLVE_TTL_MS) {
    *addr = this->resolved_;
    return true;
  }

  const std::string suffix = ".local";
  const bool is_mdns = this->host_.size() > suffix.size() &&
                       this->host_.compare(this->host_.size() - suffix.size(), suffix.size(), suffix) == 0;

#ifdef USE_MDNS
  if (is_mdns) {
    // Not getaddrinfo: this one takes an explicit timeout, and a name lookup on the bus
    // task has to be bounded.
    const std::string label = this->host_.substr(0, this->host_.size() - suffix.size());
    esp_ip4_addr_t found {};
    if (::mdns_query_a(label.c_str(), MDNS_MS, &found) == ESP_OK) {
      this->resolved_ = found.addr;
      this->resolved_at_ = millis();
      *addr = found.addr;
      ESP_LOGI(TAG, "%s is " IPSTR, this->host_.c_str(), IP2STR(&found));
      return true;
    }
    ESP_LOGW(TAG, "mDNS did not answer for %s", this->host_.c_str());
    return false;
  }
#endif

  if (!is_mdns) {
    struct addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *res = nullptr;
    // lwIP's resolver blocks with no caller-side timeout, and its retry ladder runs to
    // roughly seven seconds per configured DNS server - past the task watchdog on the
    // first unanswered lookup. Step off it for the duration rather than reboot: a
    // controller that stops writing reverts to the resting slot, which is the safe state.
#ifdef USE_ESP32
    esp_task_wdt_delete(nullptr);
#endif
    const int rc = ::getaddrinfo(this->host_.c_str(), nullptr, &hints, &res);
#ifdef USE_ESP32
    esp_task_wdt_add(nullptr);
#endif
    if (rc == 0 && res != nullptr) {
      const uint32_t found = ((struct sockaddr_in *) res->ai_addr)->sin_addr.s_addr;
      ::freeaddrinfo(res);
      this->resolved_ = found;
      this->resolved_at_ = millis();
      *addr = found;
      return true;
    }
  }

  ESP_LOGW(TAG, "could not resolve %s", this->host_.c_str());
  return false;
}

bool Datalogger::call_(const std::string &request, std::string &reply) {
  this->reachable_ = false;
  const uint32_t started = millis();

  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    return false;

  ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(this->port_);
  uint32_t resolved = 0;
  if (!this->resolve_(&resolved)) {
    ::close(fd);
    return false;
  }
  addr.sin_addr.s_addr = resolved;

  if (::connect(fd, (struct sockaddr *) &addr, sizeof(addr)) != 0 && errno != EINPROGRESS) {
    ::close(fd);
    return false;
  }
  fd_set wset;
  FD_ZERO(&wset);
  FD_SET(fd, &wset);
  struct timeval tv {};
  tv.tv_sec = CONNECT_MS / 1000;
  tv.tv_usec = (CONNECT_MS % 1000) * 1000;
  if (::select(fd + 1, nullptr, &wset, nullptr, &tv) <= 0) {
    ::close(fd);
    ESP_LOGW(TAG, "%s:%u did not accept a connection within %u ms", this->host_.c_str(),
             this->port_, (unsigned) CONNECT_MS);
    this->resolved_ = 0;
    return false;
  }
  int err = 0;
  socklen_t len = sizeof(err);
  if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
    ::close(fd);
    // Distinct from the timeout above: a refusal means something answered, so the
    // address is right and the port or the host is wrong.
    ESP_LOGW(TAG, "%s:%u refused the connection (errno %d)", this->host_.c_str(),
             this->port_, err);
    return false;
  }

  const std::string line = request + "\n";
  size_t sent = 0;
  while (sent < line.size() && millis() - started < TOTAL_MS) {
    const ssize_t n = ::send(fd, line.data() + sent, line.size() - sent, 0);
    if (n > 0) {
      sent += n;
    } else if (n < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
      break;
    } else {
      delay(5);
    }
  }
  if (sent != line.size()) {
    ::close(fd);
    return false;
  }

  reply.clear();
  char buf[512];
  for (;;) {
    const uint32_t elapsed = millis() - started;
    if (elapsed >= TOTAL_MS)
      break;
    // Clamp to what is left of the budget: a fixed window entered near the deadline
    // overruns it by its own length.
    const uint32_t left = std::min(TOTAL_MS - elapsed, RECV_MS);
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(fd, &rset);
    struct timeval rtv {};
    rtv.tv_sec = left / 1000;
    rtv.tv_usec = (left % 1000) * 1000;
    if (::select(fd + 1, &rset, nullptr, nullptr, &rtv) <= 0)
      break;
    const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
    if (n <= 0)
      break;
    reply.append(buf, n);
    if (reply.find('\n') != std::string::npos || reply.size() > 8192)
      break;
  }
  ::close(fd);

  if (reply.empty()) {
    // Silence means either another client holds the single slot, or the request envelope
    // was wrong. The two are indistinguishable from here.
    ESP_LOGW(TAG, "no reply; is something else connected to the datalogger?");
    return false;
  }
  this->reachable_ = true;
  return true;
}

bool Datalogger::read(const std::vector<uint16_t> &addrs, std::map<uint16_t, std::string> &out) {
  if (!this->configured())
    return false;

  std::string request = "{\"Get\":\"Energycontrolparameters\",\"RegControlAddr\":[";
  for (size_t i = 0; i < addrs.size(); i++) {
    if (i != 0)
      request += ",";
    request += std::to_string(addrs[i]);
  }
  request += "],\"SerialNumber\":" + std::to_string(++this->serial_) + ",\"CommandSource\":\"HA\"}";

  std::string reply;
  if (!this->call_(request, reply))
    return false;

  bool found = false;
  auto doc = json::parse_json(reply);
  if (doc.isNull())
    return false;
  JsonObject info = doc["ControlInfo"].as<JsonObject>();
  if (info.isNull())
    return false;
  for (JsonPair kv : info) {
    out[(uint16_t) atoi(kv.key().c_str())] = kv.value().as<std::string>();
    found = true;
  }
  return found;
}

bool Datalogger::write(const std::map<uint16_t, std::string> &values) {
  if (!this->configured() || values.empty())
    return false;

  std::string request = "{\"Set\":\"Energycontrolparameters\",\"SetControlInfo\":{";
  bool first = true;
  for (const auto &kv : values) {
    if (!first)
      request += ",";
    first = false;
    request += "\"" + std::to_string(kv.first) + "\":\"" + kv.second + "\"";
  }
  request += "},\"SerialNumber\":" + std::to_string(++this->serial_) + ",\"CommandSource\":\"HA\"}";

  std::string reply;
  if (!this->call_(request, reply))
    return false;

  // The key is present whether the device accepted or refused, so check the value.
  auto doc = json::parse_json(reply);
  if (doc.isNull())
    return false;
  JsonVariant result = doc["SetParameters"];
  if (result.isNull())
    return false;
  const std::string text = result.as<std::string>();
  if (text.find("fail") != std::string::npos || text.find("error") != std::string::npos) {
    ESP_LOGW(TAG, "datalogger refused the write: %s", text.c_str());
    return false;
  }
  return true;
}

}  // namespace aecc
}  // namespace esphome
