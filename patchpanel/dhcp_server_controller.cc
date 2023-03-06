// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/dhcp_server_controller.h"

#include <base/logging.h>

namespace patchpanel {

using Config = DHCPServerController::Config;

// static
std::optional<Config> Config::Create(const shill::IPAddress& host_ip,
                                     const shill::IPAddress& start_ip,
                                     const shill::IPAddress& end_ip) {
  // All the fields should be valid IPv4 IP.
  constexpr auto kValidFamily = shill::IPAddress::kFamilyIPv4;
  if (!(host_ip.IsValid() && host_ip.family() == kValidFamily &&
        start_ip.IsValid() && start_ip.family() == kValidFamily &&
        end_ip.IsValid() && end_ip.family() == kValidFamily)) {
    return std::nullopt;
  }

  // The start_ip and end_ip should be in the same subnet as host_ip.
  if (!(host_ip.CanReachAddress(start_ip) && host_ip.CanReachAddress(end_ip))) {
    return std::nullopt;
  }

  // end_ip should not be smaller than or start_ip.
  if (end_ip < start_ip) {
    return std::nullopt;
  }

  const auto netmask = shill::IPAddress::GetAddressMaskFromPrefix(
      kValidFamily, host_ip.prefix());
  return Config(host_ip.ToString(), netmask.ToString(), start_ip.ToString(),
                end_ip.ToString());
}

Config::Config(const std::string& host_ip,
               const std::string& netmask,
               const std::string& start_ip,
               const std::string& end_ip)
    : host_ip_(host_ip),
      netmask_(netmask),
      start_ip_(start_ip),
      end_ip_(end_ip) {}

std::ostream& operator<<(std::ostream& os, const Config& config) {
  os << "{host_ip: " << config.host_ip() << ", netmask: " << config.netmask()
     << ", start_ip: " << config.start_ip() << ", end_ip: " << config.end_ip()
     << "}";
  return os;
}

DHCPServerController::DHCPServerController(const std::string& ifname)
    : ifname_(ifname) {}

DHCPServerController::~DHCPServerController() {
  Stop();
}

bool DHCPServerController::Start(const Config& config) {
  LOG(INFO) << "Starting DHCP server at: " << ifname_ << ", config: " << config;

  // TODO(b/271371399): Implement the method.
  return true;
}

void DHCPServerController::Stop() {
  LOG(INFO) << "Stopping DHCP server at: " << ifname_;

  // TODO(b/271371399): Implement the method.
}

}  // namespace patchpanel
