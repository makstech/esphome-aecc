#include "datalogger.h"

#include <algorithm>
#include "esphome/components/json/json_util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cstdio>
#include <cstring>

#include <arpa/inet.h>
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
static const uint32_t RECV_MS = 1200;
static const uint32_t TOTAL_MS = 3000;

std::string Datalogger::slot(int32_t watts, uint8_t max_soc, uint8_t min_soc) {
  char buf[64];
  snprintf(buf, sizeof(buf), "1,00:00,23:59,%d,0,6,0,0,0,%u,%u", (int) watts, (unsigned) max_soc,
           (unsigned) min_soc);
  return buf;
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
  if (::inet_pton(AF_INET, this->host_.c_str(), &addr.sin_addr) != 1) {
    ::close(fd);
    ESP_LOGW(TAG, "%s is not an IPv4 address", this->host_.c_str());
    return false;
  }

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
    return false;
  }
  int err = 0;
  socklen_t len = sizeof(err);
  if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
    ::close(fd);
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
