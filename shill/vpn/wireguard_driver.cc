// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/wireguard_driver.h"

#include <base/stl_util.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/logging.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
static std::string ObjectID(const WireguardDriver*) {
  return "(wireguard_driver)";
}
}  // namespace Logging

// static
const VPNDriver::Property WireguardDriver::kProperties[] = {
    {kProviderHostProperty, 0},
    {kProviderTypeProperty, 0},
};

WireguardDriver::WireguardDriver(Manager* manager,
                                 ProcessManager* process_manager)
    : VPNDriver(
          manager, process_manager, kProperties, base::size(kProperties)) {}

// TODO(b/177876632): have the real implementation
base::TimeDelta WireguardDriver::ConnectAsync(EventHandler* event_handler) {
  SLOG(this, 2) << __func__;
  return kTimeoutNone;
}

// TODO(b/177876632): have the real implementation
void WireguardDriver::Disconnect() {
  SLOG(this, 2) << __func__;
}

// TODO(b/177876632): have the real implementation
void WireguardDriver::OnConnectTimeout() {}

// TODO(b/177876632): have the real implementation
IPConfig::Properties WireguardDriver::GetIPProperties() const {
  return {};
}

std::string WireguardDriver::GetProviderType() const {
  return kProviderWireguard;
}

}  // namespace shill
