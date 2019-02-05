// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/default_profile.h"

#include <random>

#include <base/files/file_path.h>
#include <base/strings/string_number_conversions.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/control_interface.h"
#include "shill/link_monitor.h"
#include "shill/manager.h"
#include "shill/portal_detector.h"
#include "shill/resolver.h"
#include "shill/store_interface.h"

#if !defined(DISABLE_WIFI)
#include "shill/wifi/wifi_provider.h"
#endif  // DISABLE_WIFI

using base::FilePath;
using std::string;

namespace shill {
// static
const char DefaultProfile::kDefaultId[] = "default";
// static
const char DefaultProfile::kStorageId[] = "global";
// static
const char DefaultProfile::kStorageArpGateway[] = "ArpGateway";
// static
const char DefaultProfile::kStorageCheckPortalList[] = "CheckPortalList";
// static
const char DefaultProfile::kStorageConnectionIdSalt[] = "ConnectionIdSalt";
// static
const char DefaultProfile::kStorageHostName[] = "DHCPProperty.Hostname";
// static
const char DefaultProfile::kStorageIgnoredDNSSearchPaths[] =
    "IgnoredDNSSearchPaths";
// static
const char DefaultProfile::kStorageLinkMonitorTechnologies[] =
    "LinkMonitorTechnologies";
// static
const char DefaultProfile::kStorageName[] = "Name";
// static
const char DefaultProfile::kStorageNoAutoConnectTechnologies[] =
    "NoAutoConnectTechnologies";
// static
const char DefaultProfile::kStorageOfflineMode[] = "OfflineMode";
// static
const char DefaultProfile::kStoragePortalCheckInterval[] =
    "PortalCheckInterval";
// static
const char DefaultProfile::kStorageProhibitedTechnologies[] =
    "ProhibitedTechnologies";

DefaultProfile::DefaultProfile(ControlInterface* control,
                               Metrics* metrics,
                               Manager* manager,
                               const FilePath& storage_directory,
                               const string& profile_id,
                               const Manager::Properties& manager_props)
    : Profile(
          control, metrics, manager, Identifier(profile_id),
          storage_directory, true),
      profile_id_(profile_id),
      props_(manager_props),
      random_engine_(time(nullptr)) {
  PropertyStore* store = this->mutable_store();
  store->RegisterConstBool(kArpGatewayProperty, &manager_props.arp_gateway);
  store->RegisterConstString(kCheckPortalListProperty,
                             &manager_props.check_portal_list);
  store->RegisterConstString(kIgnoredDNSSearchPathsProperty,
                             &manager_props.ignored_dns_search_paths);
  store->RegisterConstString(kLinkMonitorTechnologiesProperty,
                             &manager_props.link_monitor_technologies);
  store->RegisterConstString(kNoAutoConnectTechnologiesProperty,
                             &manager_props.no_auto_connect_technologies);
  store->RegisterConstBool(kOfflineModeProperty, &manager_props.offline_mode);
  store->RegisterConstInt32(kPortalCheckIntervalProperty,
                            &manager_props.portal_check_interval_seconds);
  store->RegisterConstString(kProhibitedTechnologiesProperty,
                             &manager_props.prohibited_technologies);
  set_persistent_profile_path(
      GetFinalStoragePath(storage_directory, Identifier(profile_id)));
}

DefaultProfile::~DefaultProfile() {}

void DefaultProfile::LoadManagerProperties(Manager::Properties* manager_props,
                                           DhcpProperties* dhcp_properties) {
  storage()->GetBool(kStorageId, kStorageArpGateway,
                     &manager_props->arp_gateway);
  storage()->GetString(kStorageId, kStorageHostName, &manager_props->host_name);
  storage()->GetBool(kStorageId, kStorageOfflineMode,
                     &manager_props->offline_mode);
  if (!storage()->GetString(kStorageId,
                            kStorageCheckPortalList,
                            &manager_props->check_portal_list)) {
    manager_props->check_portal_list = PortalDetector::kDefaultCheckPortalList;
  }
  if (!storage()->GetInt(kStorageId, kStorageConnectionIdSalt,
                         &manager_props->connection_id_salt)) {
    manager_props->connection_id_salt =
        std::uniform_int_distribution<int>()(random_engine_);
  }
  if (!storage()->GetString(kStorageId,
                            kStorageIgnoredDNSSearchPaths,
                            &manager_props->ignored_dns_search_paths)) {
    manager_props->ignored_dns_search_paths =
        Resolver::kDefaultIgnoredSearchList;
  }
  if (!storage()->GetString(kStorageId,
                            kStorageLinkMonitorTechnologies,
                            &manager_props->link_monitor_technologies)) {
    manager_props->link_monitor_technologies =
        LinkMonitor::kDefaultLinkMonitorTechnologies;
  }
  if (!storage()->GetString(kStorageId,
                            kStorageNoAutoConnectTechnologies,
                            &manager_props->no_auto_connect_technologies)) {
    manager_props->no_auto_connect_technologies = "";
  }

  // This used to be loaded from the default profile, but now it is fixed.
  manager_props->portal_http_url = PortalDetector::kDefaultHttpUrl;
  manager_props->portal_https_url = PortalDetector::kDefaultHttpsUrl;
  manager_props->portal_fallback_http_urls =
      PortalDetector::kDefaultFallbackHttpUrls;

  std::string check_interval;
  if (!storage()->GetString(kStorageId, kStoragePortalCheckInterval,
                            &check_interval) ||
      !base::StringToInt(check_interval,
                         &manager_props->portal_check_interval_seconds)) {
    manager_props->portal_check_interval_seconds =
        PortalDetector::kDefaultCheckIntervalSeconds;
  }
  if (!storage()->GetString(kStorageId,
                            kStorageProhibitedTechnologies,
                            &manager_props->prohibited_technologies)) {
    manager_props->prohibited_technologies = "";
  }
  dhcp_properties->Load(storage(), kStorageId);
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
  storage()->SetBool(kStorageId, kStorageArpGateway, props_.arp_gateway);
  storage()->SetString(kStorageId, kStorageHostName, props_.host_name);
  storage()->SetString(kStorageId, kStorageName, GetFriendlyName());
  storage()->SetBool(kStorageId, kStorageOfflineMode, props_.offline_mode);
  storage()->SetString(kStorageId,
                       kStorageCheckPortalList,
                       props_.check_portal_list);
  storage()->SetInt(kStorageId, kStorageConnectionIdSalt,
                    props_.connection_id_salt);
  storage()->SetString(kStorageId,
                       kStorageIgnoredDNSSearchPaths,
                       props_.ignored_dns_search_paths);
  storage()->SetString(kStorageId,
                       kStorageLinkMonitorTechnologies,
                       props_.link_monitor_technologies);
  storage()->SetString(kStorageId,
                       kStorageNoAutoConnectTechnologies,
                       props_.no_auto_connect_technologies);
  storage()->SetString(kStorageId,
                       kStoragePortalCheckInterval,
                       base::IntToString(props_.portal_check_interval_seconds));
  storage()->SetString(kStorageId,
                       kStorageProhibitedTechnologies,
                       props_.prohibited_technologies);
  manager()->dhcp_properties().Save(storage(), kStorageId);
  return Profile::Save();
}

bool DefaultProfile::UpdateDevice(const DeviceRefPtr& device) {
  return device->Save(storage()) && storage()->Flush();
}

#if !defined(DISABLE_WIFI)
bool DefaultProfile::UpdateWiFiProvider(const WiFiProvider& wifi_provider) {
  return wifi_provider.Save(storage()) && storage()->Flush();
}
#endif  // DISABLE_WIFI

}  // namespace shill
