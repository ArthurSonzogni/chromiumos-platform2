// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/default_profile.h"

#include <vector>

#include <base/files/file_path.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/control_interface.h"
#include "shill/link_monitor.h"
#include "shill/manager.h"
#include "shill/portal_detector.h"
#include "shill/resolver.h"
#include "shill/store_interface.h"
#include "shill/wifi_provider.h"

using base::FilePath;
using std::string;
using std::vector;

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
const char DefaultProfile::kStorageHostName[] = "HostName";
// static
const char DefaultProfile::kStorageIgnoredDNSSearchPaths[] =
    "IgnoredDNSSearchPaths";
// static
const char DefaultProfile::kStorageLinkMonitorTechnologies[] =
    "LinkMonitorTechnologies";
// static
const char DefaultProfile::kStorageName[] = "Name";
// static
const char DefaultProfile::kStorageOfflineMode[] = "OfflineMode";
// static
const char DefaultProfile::kStoragePortalURL[] = "PortalURL";
// static
const char DefaultProfile::kStoragePortalCheckInterval[] =
    "PortalCheckInterval";

DefaultProfile::DefaultProfile(ControlInterface *control,
                               Metrics *metrics,
                               Manager *manager,
                               const FilePath &storage_path,
                               const string &profile_id,
                               const Manager::Properties &manager_props)
    : Profile(control, metrics, manager, Identifier(profile_id), "", true),
      storage_path_(storage_path),
      profile_id_(profile_id),
      props_(manager_props) {
  PropertyStore *store = this->mutable_store();
  store->RegisterConstBool(kArpGatewayProperty, &manager_props.arp_gateway);
  store->RegisterConstString(kCheckPortalListProperty,
                             &manager_props.check_portal_list);
  store->RegisterConstString(kCountryProperty, &manager_props.country);
  store->RegisterConstString(kIgnoredDNSSearchPathsProperty,
                             &manager_props.ignored_dns_search_paths);
  store->RegisterConstString(kLinkMonitorTechnologiesProperty,
                             &manager_props.link_monitor_technologies);
  store->RegisterConstBool(kOfflineModeProperty, &manager_props.offline_mode);
  store->RegisterConstString(kPortalURLProperty, &manager_props.portal_url);
  store->RegisterConstInt32(kPortalCheckIntervalProperty,
                            &manager_props.portal_check_interval_seconds);
}

DefaultProfile::~DefaultProfile() {}

void DefaultProfile::LoadManagerProperties(Manager::Properties *manager_props) {
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
  if (!storage()->GetString(kStorageId, kStoragePortalURL,
                            &manager_props->portal_url)) {
    manager_props->portal_url = PortalDetector::kDefaultURL;
  }
  std::string check_interval;
  if (!storage()->GetString(kStorageId, kStoragePortalCheckInterval,
                            &check_interval) ||
      !base::StringToInt(check_interval,
                         &manager_props->portal_check_interval_seconds)) {
    manager_props->portal_check_interval_seconds =
        PortalDetector::kDefaultCheckIntervalSeconds;
  }
}

bool DefaultProfile::ConfigureService(const ServiceRefPtr &service) {
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
  storage()->SetString(kStorageId,
                       kStorageIgnoredDNSSearchPaths,
                       props_.ignored_dns_search_paths);
  storage()->SetString(kStorageId,
                       kStorageLinkMonitorTechnologies,
                       props_.link_monitor_technologies);
  storage()->SetString(kStorageId,
                       kStoragePortalURL,
                       props_.portal_url);
  storage()->SetString(kStorageId,
                       kStoragePortalCheckInterval,
                       base::IntToString(props_.portal_check_interval_seconds));
  return Profile::Save();
}

bool DefaultProfile::UpdateDevice(const DeviceRefPtr &device) {
  return device->Save(storage()) && storage()->Flush();
}

bool DefaultProfile::UpdateWiFiProvider(const WiFiProvider &wifi_provider) {
  return wifi_provider.Save(storage()) && storage()->Flush();
}

bool DefaultProfile::GetStoragePath(FilePath *path) {
  *path = storage_path_.Append(base::StringPrintf("%s.profile",
                                                  profile_id_.c_str()));
  return true;
}

}  // namespace shill
