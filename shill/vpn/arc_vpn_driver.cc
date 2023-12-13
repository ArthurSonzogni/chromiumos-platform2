// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/arc_vpn_driver.h"

#include <fcntl.h>
#include <unistd.h>

#include <iterator>
#include <memory>
#include <utility>

#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/string_split.h>
#include <chromeos/dbus/service_constants.h>
#include <net-base/network_config.h>

#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/vpn/vpn_provider.h"
#include "shill/vpn/vpn_service.h"
#include "shill/vpn/vpn_types.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
}  // namespace Logging

const VPNDriver::Property ArcVpnDriver::kProperties[] = {
    {kProviderHostProperty, 0},
    {kProviderTypeProperty, 0},
    {kArcVpnTunnelChromeProperty, 0}};

ArcVpnDriver::ArcVpnDriver(Manager* manager, ProcessManager* process_manager)
    : VPNDriver(manager,
                process_manager,
                VPNType::kARC,
                kProperties,
                std::size(kProperties)) {}

base::TimeDelta ArcVpnDriver::ConnectAsync(EventHandler* handler) {
  SLOG(2) << __func__;
  // Nothing to do here since ARC already finish connecting to VPN
  // before Chrome calls Service::OnConnect. Just return success.
  metrics()->SendEnumToUMA(Metrics::kMetricVpnDriver, Metrics::kVpnDriverArc);
  dispatcher()->PostTask(FROM_HERE,
                         base::BindOnce(&ArcVpnDriver::InvokeEventHandler,
                                        weak_factory_.GetWeakPtr(), handler));
  return kTimeoutNone;
}

void ArcVpnDriver::InvokeEventHandler(EventHandler* handler) {
  std::string if_name(VPNProvider::kArcBridgeIfName);
  int if_index = manager()->device_info()->GetIndex(if_name);
  if (if_index == -1) {
    handler->OnDriverFailure(Service::kFailureInternal,
                             "Failed to get interface index for arc bridge");
    return;
  }

  handler->OnDriverConnected(if_name, if_index);
}

void ArcVpnDriver::Disconnect() {
  SLOG(2) << __func__;
}

void ArcVpnDriver::OnConnectTimeout() {
  NOTREACHED();
}

std::unique_ptr<net_base::NetworkConfig> ArcVpnDriver::GetNetworkConfig()
    const {
  SLOG(2) << __func__;
  // Currently L3 settings for ARC VPN are set from Chrome as
  // StaticIPProperty before connecting, so this will be mostly empty.
  auto network_config = std::make_unique<net_base::NetworkConfig>();
  // ARC always sets IncludedRoutes through StaticIPConfig.
  network_config->ipv4_default_route = false;
  // IPv6 is not currently supported.  If the VPN is enabled, block all
  // IPv6 traffic so there is no "leak" past the VPN.
  network_config->ipv6_blackhole_route = true;
  return network_config;
}

}  // namespace shill
