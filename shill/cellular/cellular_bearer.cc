// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_bearer.h"

#include <memory>
#include <string>
#include <vector>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <ModemManager/ModemManager.h>
#include <net-base/ip_address.h>
#include <net-base/ipv4_address.h>
#include <net-base/ipv6_address.h>
#include <net-base/network_config.h>

#include "shill/control_interface.h"
#include "shill/dbus/dbus_properties_proxy.h"
#include "shill/logging.h"
#include "shill/store/key_value_store.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCellular;
}  // namespace Logging

// static
// These bearer properties have to match the ones in
// org.freedesktop.ModemManager1.Bearer.xml
const char CellularBearer::kMMApnProperty[] = "apn";
const char CellularBearer::kMMApnTypeProperty[] = "apn-type";
const char CellularBearer::kMMUserProperty[] = "user";
const char CellularBearer::kMMPasswordProperty[] = "password";
const char CellularBearer::kMMAllowedAuthProperty[] = "allowed-auth";
const char CellularBearer::kMMAllowRoamingProperty[] = "allow-roaming";
const char CellularBearer::kMMIpTypeProperty[] = "ip-type";
const char CellularBearer::kMMMultiplexProperty[] = "multiplex";
const char CellularBearer::kMMForceProperty[] = "force";
const char CellularBearer::kMMProfileIdProperty[] = "profile-id";

namespace {

const char kPropertyAddress[] = "address";
const char kPropertyDNS1[] = "dns1";
const char kPropertyDNS2[] = "dns2";
const char kPropertyDNS3[] = "dns3";
const char kPropertyGateway[] = "gateway";
const char kPropertyMethod[] = "method";
const char kPropertyPrefix[] = "prefix";
const char kPropertyMtu[] = "mtu";

CellularBearer::IPConfigMethod ConvertMMBearerIPConfigMethod(uint32_t method) {
  switch (method) {
    case MM_BEARER_IP_METHOD_PPP:
      return CellularBearer::IPConfigMethod::kPPP;
    case MM_BEARER_IP_METHOD_STATIC:
      return CellularBearer::IPConfigMethod::kStatic;
    case MM_BEARER_IP_METHOD_DHCP:
      return CellularBearer::IPConfigMethod::kDHCP;
    default:
      return CellularBearer::IPConfigMethod::kUnknown;
  }
}

}  // namespace

CellularBearer::CellularBearer(ControlInterface* control_interface,
                               const RpcIdentifier& dbus_path,
                               const std::string& dbus_service)
    : control_interface_(control_interface),
      dbus_path_(dbus_path),
      dbus_service_(dbus_service) {
  CHECK(control_interface_);
}

CellularBearer::~CellularBearer() = default;

bool CellularBearer::Init() {
  SLOG(3) << __func__ << ": path='" << dbus_path_.value() << "', service='"
          << dbus_service_ << "'";

  dbus_properties_proxy_ =
      control_interface_->CreateDBusPropertiesProxy(dbus_path_, dbus_service_);
  // It is possible that ProxyFactory::CreateDBusPropertiesProxy() returns
  // nullptr as the bearer DBus object may no longer exist.
  if (!dbus_properties_proxy_) {
    LOG(WARNING) << "Failed to create DBus properties proxy for bearer '"
                 << dbus_path_.value() << "'. Bearer is likely gone.";
    return false;
  }

  dbus_properties_proxy_->SetPropertiesChangedCallback(base::BindRepeating(
      &CellularBearer::OnPropertiesChanged, base::Unretained(this)));
  UpdateProperties();
  return true;
}

namespace {
// Gets DNS servers from |properties|, and stores them in |dns_servers|.
void GetDNSFromProperties(const KeyValueStore& properties,
                          std::vector<net_base::IPAddress>& dns_servers) {
  for (const char* key : {kPropertyDNS1, kPropertyDNS2, kPropertyDNS3}) {
    if (properties.Contains<std::string>(key)) {
      const std::string& value = properties.Get<std::string>(key);
      const auto dns = net_base::IPAddress::CreateFromString(value);
      if (!dns.has_value()) {
        LOG(WARNING) << "Failed to get DNS from value: " << value
                     << ", ignoring key: " << key;
        continue;
      }
      dns_servers.push_back(*dns);
    }
  }
}
}  // namespace

void CellularBearer::SetIPv4MethodAndConfig(const KeyValueStore& properties) {
  uint32_t method = MM_BEARER_IP_METHOD_UNKNOWN;
  if (properties.Contains<uint32_t>(kPropertyMethod)) {
    method = properties.Get<uint32_t>(kPropertyMethod);
  } else {
    SLOG(2) << "Bearer '" << dbus_path_.value()
            << "' does not specify an IP configuration method.";
  }

  ipv4_config_method_ = ConvertMMBearerIPConfigMethod(method);

  // Additional settings are only expected in either static or dynamic IP
  // addressing, so we can bail out early otherwise.
  if (ipv4_config_method_ != IPConfigMethod::kStatic &&
      ipv4_config_method_ != IPConfigMethod::kDHCP) {
    ipv4_config_.reset();
    return;
  }

  ipv4_config_ = std::make_unique<net_base::NetworkConfig>();

  // DNS servers and MTU are reported by the network via PCOs, so we may have
  // them both when using static or dynamic IP addressing.
  GetDNSFromProperties(properties, ipv4_config_->dns_servers);
  if (properties.Contains<uint32_t>(kPropertyMtu)) {
    ipv4_config_->mtu = properties.Get<uint32_t>(kPropertyMtu);
    if (ipv4_config_->mtu < net_base::NetworkConfig::kMinIPv4MTU) {
      LOG(WARNING) << "MTU " << *ipv4_config_->mtu
                   << " for IPv4 config is too small, adjusting up to "
                   << net_base::NetworkConfig::kMinIPv4MTU;
      ipv4_config_->mtu = net_base::NetworkConfig::kMinIPv4MTU;
    }
  }

  // Try to get an IP address.
  if (!properties.Contains<std::string>(kPropertyAddress)) {
    return;
  }
  const auto& addr = properties.Get<std::string>(kPropertyAddress);

  uint32_t prefix = properties.Lookup<uint32_t>(
      kPropertyPrefix, net_base::IPv4CIDR::kMaxPrefixLength);

  ipv4_config_->ipv4_address =
      net_base::IPv4CIDR::CreateFromStringAndPrefix(addr, prefix);
  if (!ipv4_config_->ipv4_address.has_value()) {
    LOG(WARNING) << "Failed to parse IPv4 address from " << addr << "/"
                 << prefix;
  }

  // If we have an IP address, we may also have a gateway.
  if (properties.Contains<std::string>(kPropertyGateway)) {
    const auto& gateway = properties.Get<std::string>(kPropertyGateway);
    ipv4_config_->ipv4_gateway =
        net_base::IPv4Address::CreateFromString(gateway);
    if (!ipv4_config_->ipv4_gateway.has_value()) {
      LOG(WARNING) << "Failed to parse IPv4 gateway from " << gateway;
    }
  }
}

void CellularBearer::SetIPv6MethodAndConfig(const KeyValueStore& properties) {
  uint32_t method = MM_BEARER_IP_METHOD_UNKNOWN;
  if (properties.Contains<uint32_t>(kPropertyMethod)) {
    method = properties.Get<uint32_t>(kPropertyMethod);
  } else {
    SLOG(2) << "Bearer '" << dbus_path_.value()
            << "' does not specify an IP configuration method.";
  }

  ipv6_config_method_ = ConvertMMBearerIPConfigMethod(method);

  // Additional settings are only expected in either static or dynamic IP
  // addressing, so we can bail out early otherwise.
  if (ipv6_config_method_ != IPConfigMethod::kStatic &&
      ipv6_config_method_ != IPConfigMethod::kDHCP) {
    ipv6_config_.reset();
    return;
  }

  ipv6_config_ = std::make_unique<net_base::NetworkConfig>();

  // DNS servers and MTU are reported by the network via PCOs, so we may have
  // them both when using static or dynamic IP addressing.
  GetDNSFromProperties(properties, ipv6_config_->dns_servers);
  if (properties.Contains<uint32_t>(kPropertyMtu)) {
    ipv6_config_->mtu = properties.Get<uint32_t>(kPropertyMtu);
    if (ipv6_config_->mtu < net_base::NetworkConfig::kMinIPv6MTU) {
      LOG(WARNING) << "MTU " << *ipv6_config_->mtu
                   << " for IPv6 config is too small, adjusting up to "
                   << net_base::NetworkConfig::kMinIPv6MTU;
      ipv6_config_->mtu = net_base::NetworkConfig::kMinIPv6MTU;
    }
  }

  // If the modem didn't do its own IPv6 SLAAC, it may still report a link-local
  // address that we need to configure before running host SLAAC. Therefore,
  // always try to process kPropertyAddress if given. There is not much benefit
  // in ensuring the method is kStatic or kDHCP, because ModemManager will never
  // set the IP address unless it's one of those two.
  if (!properties.Contains<std::string>(kPropertyAddress)) {
    return;
  }
  const auto& addr = properties.Get<std::string>(kPropertyAddress);

  uint32_t prefix;
  if (!properties.Contains<uint32_t>(kPropertyPrefix)) {
    prefix = net_base::IPv6CIDR::kMaxPrefixLength;
  } else {
    prefix = properties.Get<uint32_t>(kPropertyPrefix);
  }

  const auto& cidr =
      net_base::IPv6CIDR::CreateFromStringAndPrefix(addr, prefix);
  if (!cidr.has_value()) {
    LOG(WARNING) << "Failed to parse IPv6 address from " << addr << "/"
                 << prefix;
  } else {
    ipv6_config_->ipv6_addresses.push_back(*cidr);
  }

  // If we have an IP address, we may also have a gateway.
  if (properties.Contains<std::string>(kPropertyGateway)) {
    const auto& gateway = properties.Get<std::string>(kPropertyGateway);
    ipv6_config_->ipv6_gateway =
        net_base::IPv6Address::CreateFromString(gateway);
    if (!ipv6_config_->ipv6_gateway.has_value()) {
      LOG(WARNING) << "Failed to parse IPv6 gateway from " << gateway;
    }
  }
}

void CellularBearer::ResetProperties() {
  connected_ = false;
  apn_.clear();
  apn_types_.clear();
  data_interface_.clear();
  ipv4_config_method_ = IPConfigMethod::kUnknown;
  ipv4_config_.reset();
  ipv6_config_method_ = IPConfigMethod::kUnknown;
  ipv6_config_.reset();
}

void CellularBearer::UpdateProperties() {
  ResetProperties();

  if (!dbus_properties_proxy_)
    return;

  auto properties = dbus_properties_proxy_->GetAll(MM_DBUS_INTERFACE_BEARER);
  OnPropertiesChanged(MM_DBUS_INTERFACE_BEARER, properties);
}

void CellularBearer::OnPropertiesChanged(
    const std::string& interface, const KeyValueStore& changed_properties) {
  SLOG(3) << __func__ << ": path=" << dbus_path_.value()
          << ", interface=" << interface;

  if (interface != MM_DBUS_INTERFACE_BEARER)
    return;

  if (changed_properties.Contains<KeyValueStore>(
          MM_BEARER_PROPERTY_PROPERTIES)) {
    KeyValueStore properties =
        changed_properties.Get<KeyValueStore>(MM_BEARER_PROPERTY_PROPERTIES);
    if (properties.Contains<std::string>(kMMApnProperty)) {
      apn_ = properties.Get<std::string>(kMMApnProperty);
    }
    if (properties.Contains<uint32_t>(kMMApnTypeProperty)) {
      uint32_t apns_mask = properties.Get<uint32_t>(kMMApnTypeProperty);
      if (apns_mask & MM_BEARER_APN_TYPE_DEFAULT)
        apn_types_.push_back(ApnList::ApnType::kDefault);
      if (apns_mask & MM_BEARER_APN_TYPE_INITIAL)
        apn_types_.push_back(ApnList::ApnType::kAttach);
      if (apns_mask & MM_BEARER_APN_TYPE_TETHERING)
        apn_types_.push_back(ApnList::ApnType::kDun);
    }
  }

  if (changed_properties.Contains<bool>(MM_BEARER_PROPERTY_CONNECTED)) {
    connected_ = changed_properties.Get<bool>(MM_BEARER_PROPERTY_CONNECTED);
  }

  if (changed_properties.Contains<std::string>(MM_BEARER_PROPERTY_INTERFACE)) {
    data_interface_ =
        changed_properties.Get<std::string>(MM_BEARER_PROPERTY_INTERFACE);
  }

  if (changed_properties.Contains<KeyValueStore>(
          MM_BEARER_PROPERTY_IP4CONFIG)) {
    KeyValueStore ipconfig =
        changed_properties.Get<KeyValueStore>(MM_BEARER_PROPERTY_IP4CONFIG);
    SetIPv4MethodAndConfig(ipconfig);
  }
  if (changed_properties.Contains<KeyValueStore>(
          MM_BEARER_PROPERTY_IP6CONFIG)) {
    KeyValueStore ipconfig =
        changed_properties.Get<KeyValueStore>(MM_BEARER_PROPERTY_IP6CONFIG);
    SetIPv6MethodAndConfig(ipconfig);
  }
}

}  // namespace shill
