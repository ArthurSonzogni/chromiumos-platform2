// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/default_profile.h"

#include <string>
#include <string_view>
#include <vector>

#include <base/files/file_path.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/manager.h"
#include "shill/network/portal_detector.h"
#include "shill/resolver.h"
#include "shill/store/property_accessor.h"
#include "shill/store/store_interface.h"

namespace shill {

namespace {
// UseSwanctlDriver was removed in crrev.com/c/3857326.
// This was left here to remove UseSwanctlDriver entries from profiles.
constexpr char kStorageUseSwanctlDriver[] = "UseSwanctlDriver";

// Removed in crrev/c/5040178. Left to remove entries from profile.
constexpr char kStorageIgnoredDNSSearchPaths[] = "IgnoredDNSSearchPaths";
}  // namespace

// static
const char DefaultProfile::kDefaultId[] = "default";
// static
const char DefaultProfile::kStorageArpGateway[] = "ArpGateway";
// static
const char DefaultProfile::kStorageCheckPortalList[] = "CheckPortalList";
// static
const char DefaultProfile::kStorageName[] = "Name";
// static
const char DefaultProfile::kStorageNoAutoConnectTechnologies[] =
    "NoAutoConnectTechnologies";
// static
const char DefaultProfile::kStorageProhibitedTechnologies[] =
    "ProhibitedTechnologies";
// b/221171651: This string must stay consistent with the storage id used
// previously by DhcpProperties.
// static
const char DefaultProfile::kStorageDhcpHostname[] = "Hostname";
// static
const char DefaultProfile::kStorageWifiGlobalFTEnabled[] =
    "WiFi.GlobalFTEnabled";

DefaultProfile::DefaultProfile(Manager* manager,
                               const base::FilePath& storage_directory,
                               const std::string& profile_id,
                               const ManagerProperties& manager_props)
    : Profile(manager, Identifier(profile_id), storage_directory, true),
      profile_id_(profile_id),
      props_(manager_props) {
  PropertyStore* store = this->mutable_store();
  store->RegisterConstBool(kArpGatewayProperty, &manager_props.arp_gateway);
  store->RegisterConstString(kCheckPortalListProperty,
                             &manager_props.check_portal_list);
  store->RegisterConstString(kNoAutoConnectTechnologiesProperty,
                             &manager_props.no_auto_connect_technologies);
  store->RegisterConstString(kProhibitedTechnologiesProperty,
                             &manager_props.prohibited_technologies);
  HelpRegisterConstDerivedBool(kWifiGlobalFTEnabledProperty,
                               &DefaultProfile::GetFTEnabled);
  set_persistent_profile_path(
      GetFinalStoragePath(storage_directory, Identifier(profile_id)));
}

DefaultProfile::~DefaultProfile() = default;

void DefaultProfile::HelpRegisterConstDerivedBool(
    std::string_view name, bool (DefaultProfile::*get)(Error*)) {
  this->mutable_store()->RegisterDerivedBool(
      name, BoolAccessor(new CustomAccessor<DefaultProfile, bool>(
                this, get, nullptr, nullptr)));
}

bool DefaultProfile::GetFTEnabled(Error* error) {
  return manager()->GetFTEnabled(error);
}

void DefaultProfile::LoadManagerProperties(ManagerProperties* manager_props) {
  storage()->GetBool(kStorageId, kStorageArpGateway,
                     &manager_props->arp_gateway);
  storage()->GetBool(kStorageId, kStorageEnableRFC8925,
                     &manager_props->enable_rfc_8925);
  if (!storage()->GetString(kStorageId, kStorageCheckPortalList,
                            &manager_props->check_portal_list)) {
    manager_props->check_portal_list = PortalDetector::kDefaultCheckPortalList;
  }
  if (!storage()->GetString(kStorageId, kStorageNoAutoConnectTechnologies,
                            &manager_props->no_auto_connect_technologies)) {
    manager_props->no_auto_connect_technologies = "";
  }

  // This used to be loaded from the default profile, but now it is fixed.
  manager_props->portal_http_url = PortalDetector::kDefaultHttpUrl;
  manager_props->portal_https_url = PortalDetector::kDefaultHttpsUrl;
  manager_props->portal_fallback_http_urls =
      std::vector<std::string>(PortalDetector::kDefaultFallbackHttpUrls.begin(),
                               PortalDetector::kDefaultFallbackHttpUrls.end());
  manager_props->portal_fallback_https_urls = std::vector<std::string>(
      PortalDetector::kDefaultFallbackHttpsUrls.begin(),
      PortalDetector::kDefaultFallbackHttpsUrls.end());

  if (!storage()->GetString(kStorageId, kStorageProhibitedTechnologies,
                            &manager_props->prohibited_technologies)) {
    manager_props->prohibited_technologies = "";
  }

  if (!storage()->GetString(kStorageId, kStorageDhcpHostname,
                            &manager_props->dhcp_hostname)) {
    manager_props->dhcp_hostname = "";
  }

  bool ft_enabled;
  if (storage()->GetBool(kStorageId, kStorageWifiGlobalFTEnabled,
                         &ft_enabled)) {
    manager_props->ft_enabled = ft_enabled;
  }

  manager_props->request_scan_type = kWiFiRequestScanTypeDefault;

  manager_props->disconnect_wifi_on_ethernet =
      ManagerProperties::DisconnectWiFiOnEthernet::kOff;
  std::string disconnect_wifi_property;
  if (storage()->GetString(kStorageId, kStorageDisableWiFiOnEthernet,
                           &disconnect_wifi_property)) {
    if (base::EqualsCaseInsensitiveASCII(
            disconnect_wifi_property, kStorageDisableWiFiOnEthernetConnected)) {
      manager_props->disconnect_wifi_on_ethernet =
          ManagerProperties::DisconnectWiFiOnEthernet::kConnected;
    }
    if (base::EqualsCaseInsensitiveASCII(disconnect_wifi_property,
                                         kStorageDisableWiFiOnEthernetOnline)) {
      manager_props->disconnect_wifi_on_ethernet =
          ManagerProperties::DisconnectWiFiOnEthernet::kOnline;
    }
  }
}

bool DefaultProfile::ConfigureService(const ServiceRefPtr& service) {
  if (Profile::ConfigureService(service)) {
    return true;
  }
  if (service->technology() == Technology::kEthernet) {
    // Ethernet services should have an affinity towards the default profile,
    // so even if a new Ethernet service has no known configuration, accept
    // it anyway.
    UpdateService(service);
    service->SetProfile(this);
    return true;
  }
  return false;
}

bool DefaultProfile::Save() {
  // UseSwanctlDriver was removed in crrev.com/c/3857326.
  storage()->DeleteKey(kStorageId, kStorageUseSwanctlDriver);
  // IgnoredDNSSearchPaths was removed in crrev/c/5040178Í.
  storage()->DeleteKey(kStorageId, kStorageIgnoredDNSSearchPaths);

  storage()->SetBool(kStorageId, kStorageArpGateway, props_.arp_gateway);
  storage()->SetBool(kStorageId, kStorageEnableRFC8925, props_.enable_rfc_8925);
  storage()->SetString(kStorageId, kStorageName, GetFriendlyName());
  storage()->SetString(kStorageId, kStorageCheckPortalList,
                       props_.check_portal_list);
  storage()->SetString(kStorageId, kStorageNoAutoConnectTechnologies,
                       props_.no_auto_connect_technologies);
  storage()->SetString(kStorageId, kStorageProhibitedTechnologies,
                       props_.prohibited_technologies);
  if (!props_.dhcp_hostname.empty()) {
    storage()->SetString(kStorageId, kStorageDhcpHostname,
                         props_.dhcp_hostname);
  }
  if (props_.ft_enabled.has_value()) {
    storage()->SetBool(kStorageId, kStorageWifiGlobalFTEnabled,
                       props_.ft_enabled.value());
  }
  switch (props_.disconnect_wifi_on_ethernet) {
    case ManagerProperties::DisconnectWiFiOnEthernet::kOff:
      storage()->DeleteKey(kStorageId, kStorageDisableWiFiOnEthernet);
      break;
    case ManagerProperties::DisconnectWiFiOnEthernet::kConnected:
      storage()->SetString(kStorageId, kStorageDisableWiFiOnEthernet,
                           kStorageDisableWiFiOnEthernetConnected);
      break;
    case ManagerProperties::DisconnectWiFiOnEthernet::kOnline:
      storage()->SetString(kStorageId, kStorageDisableWiFiOnEthernet,
                           kStorageDisableWiFiOnEthernetOnline);
      break;
  }
  return Profile::Save();
}

bool DefaultProfile::UpdateDevice(const DeviceRefPtr& device) {
  return device->Save(storage()) && storage()->Flush();
}

}  // namespace shill
