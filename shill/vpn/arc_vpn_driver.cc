//
// Copyright 2017 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include "shill/vpn/arc_vpn_driver.h"

#include <fcntl.h>
#include <unistd.h>

#include <utility>

#include <base/strings/string_split.h>

#include "shill/connection.h"
#include "shill/logging.h"
#include "shill/static_ip_parameters.h"
#include "shill/vpn/vpn_provider.h"
#include "shill/vpn/vpn_service.h"

namespace shill {

namespace {

const char* const kDnsAndRoutingProperties[] = {
    kDomainNameProperty,
    kNameServersProperty,
    kSearchDomainsProperty,
    kIncludedRoutesProperty,
    kExcludedRoutesProperty,
};

}  // namespace

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
static std::string ObjectID(const ArcVpnDriver* v) {
  return "(arc_vpn_driver)";
}
}  // namespace Logging

const VPNDriver::Property ArcVpnDriver::kProperties[] = {
    {kProviderHostProperty, 0},
    {kProviderTypeProperty, 0},
    {kArcVpnTunnelChromeProperty, 0}};

ArcVpnDriver::ArcVpnDriver(ControlInterface* control,
                           EventDispatcher* dispatcher,
                           Metrics* metrics,
                           Manager* manager,
                           DeviceInfo* device_info)
    : VPNDriver(dispatcher, manager, kProperties, arraysize(kProperties)),
      control_(control),
      dispatcher_(dispatcher),
      metrics_(metrics),
      device_info_(device_info) {}

ArcVpnDriver::~ArcVpnDriver() {
  Cleanup();
}

void ArcVpnDriver::Cleanup() {
  if (device_) {
    device_->DropConnection();
    device_->SetEnabled(false);
    device_ = nullptr;
  }

  if (service_) {
    service_->SetState(Service::kStateIdle);
    service_ = nullptr;
  }
}

void ArcVpnDriver::Connect(const VPNServiceRefPtr& service, Error* error) {
  SLOG(this, 2) << __func__;

  device_ = manager()->vpn_provider()->arc_device();
  if (!device_) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kNotFound, "arc_device is not found");
    return;
  }

  service_ = service;
  // This sets the has_ever_connected flag.
  service_->SetState(Service::kStateConnected);

  IPConfig::Properties ip_properties;
  if (args()->LookupString(kArcVpnTunnelChromeProperty, "false") != "true") {
    // If Chrome tunneling is disabled, traffic will not be passed through
    // this interface, but users will still be able to see VPN status
    // and disconnect the VPN through the UI.  In this case the IP address
    // and gateway should still be reflected in the properties, but the
    // DNS and routing information should be zapped so that Chrome
    // traffic falls through to the next highest service.
    Error error;
    std::string prefix(StaticIPParameters::kConfigKeyPrefix);
    for (const auto& property : kDnsAndRoutingProperties) {
      service_->mutable_store()->ClearProperty(prefix + property, &error);
    }
    if (!error.IsSuccess()) {
      LOG(ERROR) << "Unable to clear VPN IP properties: " << error.message();
    }
  } else {
    // IPv6 is not currently supported.  If the VPN is enabled, block all
    // IPv6 traffic so there is no "leak" past the VPN.
    ip_properties.blackhole_ipv6 = true;
  }

  // This will always create the per-device routing table, but it might
  // not have any routes if |ip_properties.routes| is empty.
  ip_properties.allowed_uids = manager()->browser_traffic_uids();
  CHECK(!ip_properties.allowed_uids.empty());

  ip_properties.default_route = false;

  device_->SetEnabled(true);
  device_->SelectService(service_);

  // Device::OnIPConfigUpdated() will apply the StaticIPConfig properties.
  device_->UpdateIPConfig(ip_properties);
  device_->SetLooseRouting(true);

  service_->SetState(Service::kStateOnline);

  metrics_->SendEnumToUMA(Metrics::kMetricVpnDriver,
                          Metrics::kVpnDriverArc,
                          Metrics::kMetricVpnDriverMax);
}

bool ArcVpnDriver::ClaimInterface(const std::string& link_name,
                                  int interface_index) {
  // This never happens to our interface, because it exists before
  // shill starts up.
  return false;
}

void ArcVpnDriver::Disconnect() {
  SLOG(this, 2) << __func__;
  Cleanup();
}

void ArcVpnDriver::OnConnectionDisconnected() {
  SLOG(this, 2) << __func__;
}

std::string ArcVpnDriver::GetProviderType() const {
  return std::string(kProviderArcVpn);
}

}  // namespace shill
