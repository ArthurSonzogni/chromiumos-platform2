// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn_provider.h"

#include <algorithm>
#include <memory>

#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/error.h"
#include "shill/l2tp_ipsec_driver.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/openvpn_driver.h"
#include "shill/profile.h"
#include "shill/store_interface.h"
#include "shill/vpn_service.h"

using std::set;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
static string ObjectID(const VPNProvider *v) { return "(vpn_provider)"; }
}

VPNProvider::VPNProvider(ControlInterface *control_interface,
                         EventDispatcher *dispatcher,
                         Metrics *metrics,
                         Manager *manager)
    : control_interface_(control_interface),
      dispatcher_(dispatcher),
      metrics_(metrics),
      manager_(manager) {}

VPNProvider::~VPNProvider() {}

void VPNProvider::Start() {}

void VPNProvider::Stop() {}

// static
bool VPNProvider::GetServiceParametersFromArgs(const KeyValueStore &args,
                                               string *type_ptr,
                                               string *name_ptr,
                                               string *host_ptr,
                                               Error *error) {
  SLOG(nullptr, 2) << __func__;
  string type = args.LookupString(kProviderTypeProperty, "");
  if (type.empty()) {
    Error::PopulateAndLog(
        error, Error::kNotSupported, "Missing VPN type property.");
    return false;
  }

  string host = args.LookupString(kProviderHostProperty, "");
  if (host.empty()) {
    Error::PopulateAndLog(
        error, Error::kNotSupported, "Missing VPN host property.");
    return false;
  }

  *type_ptr = type,
  *host_ptr = host,
  *name_ptr = args.LookupString(kNameProperty, "");

  return true;
}

ServiceRefPtr VPNProvider::GetService(const KeyValueStore &args,
                                      Error *error) {
  SLOG(this, 2) << __func__;
  string type;
  string name;
  string host;

  if (!GetServiceParametersFromArgs(args, &type, &name, &host, error)) {
    return nullptr;
  }

  string storage_id = VPNService::CreateStorageIdentifier(args, error);
  if (storage_id.empty()) {
    return nullptr;
  }

  // Find a service in the provider list which matches these parameters.
  VPNServiceRefPtr service = FindService(type, name, host);
  if (service == nullptr) {
    service = CreateService(type, name, storage_id, error);
  }
  return service;
}

ServiceRefPtr VPNProvider::FindSimilarService(const KeyValueStore &args,
                                              Error *error) const {
  SLOG(this, 2) << __func__;
  string type;
  string name;
  string host;

  if (!GetServiceParametersFromArgs(args, &type, &name, &host, error)) {
    return nullptr;
  }

  // Find a service in the provider list which matches these parameters.
  VPNServiceRefPtr service = FindService(type, name, host);
  if (!service) {
    error->Populate(Error::kNotFound, "Matching service was not found");
  }

  return service;
}

bool VPNProvider::OnDeviceInfoAvailable(const string &link_name,
                                        int interface_index) {
  for (const auto &service : services_) {
    if (service->driver()->ClaimInterface(link_name, interface_index)) {
      return true;
    }
  }

  return false;
}

void VPNProvider::RemoveService(VPNServiceRefPtr service) {
  const auto it = std::find(services_.begin(), services_.end(), service);
  if (it != services_.end()) {
    services_.erase(it);
  }
}

void VPNProvider::CreateServicesFromProfile(const ProfileRefPtr &profile) {
  SLOG(this, 2) << __func__;
  const StoreInterface *storage = profile->GetConstStorage();
  for (const auto &group : storage->GetGroupsWithKey(kProviderTypeProperty)) {
    if (!StartsWithASCII(group, "vpn_", false)) {
      continue;
    }

    string type;
    if (!storage->GetString(group, kProviderTypeProperty, &type)) {
      LOG(ERROR) << "Group " << group << " is missing the "
                 << kProviderTypeProperty << " property.";
      continue;
    }

    string name;
    if (!storage->GetString(group, kNameProperty, &name)) {
      LOG(ERROR) << "Group " << group << " is missing the "
                 << kNameProperty << " property.";
      continue;
    }

    string host;
    if (!storage->GetString(group, kProviderHostProperty, &host)) {
      LOG(ERROR) << "Group " << group << " is missing the "
                 << kProviderHostProperty << " property.";
      continue;
    }

    VPNServiceRefPtr service = FindService(type, name, host);
    if (service != nullptr) {
      // If the service already exists, it does not need to be configured,
      // since PushProfile would have already called ConfigureService on it.
      SLOG(this, 2) << "Service already exists " << group;
      continue;
    }

    Error error;
    service = CreateService(type, name, group, &error);

    if (service == nullptr) {
      LOG(ERROR) << "Could not create service for " << group;
      continue;
    }

    if (!profile->ConfigureService(service)) {
      LOG(ERROR) << "Could not configure service for " << group;
      continue;
    }
  }
}

VPNServiceRefPtr VPNProvider::CreateServiceInner(const string &type,
                                                 const string &name,
                                                 const string &storage_id,
                                                 Error *error) {
  SLOG(this, 2) << __func__ << " type " << type << " name " << name
                << " storage id " << storage_id;
#if defined(DISABLE_VPN)

  Error::PopulateAndLog(error, Error::kNotSupported, "VPN is not supported.");
  return nullptr;

#else

  std::unique_ptr<VPNDriver> driver;
  if (type == kProviderOpenVpn) {
    driver.reset(new OpenVPNDriver(
        control_interface_, dispatcher_, metrics_, manager_,
        manager_->device_info(), manager_->glib()));
  } else if (type == kProviderL2tpIpsec) {
    driver.reset(new L2TPIPSecDriver(
        control_interface_, dispatcher_, metrics_, manager_,
        manager_->device_info(), manager_->glib()));
  } else {
    Error::PopulateAndLog(
        error, Error::kNotSupported, "Unsupported VPN type: " + type);
    return nullptr;
  }

  VPNServiceRefPtr service = new VPNService(
      control_interface_, dispatcher_, metrics_, manager_, driver.release());
  service->set_storage_id(storage_id);
  service->InitDriverPropertyStore();
  if (!name.empty()) {
    service->set_friendly_name(name);
  }
  return service;

#endif  // DISABLE_VPN
}

VPNServiceRefPtr VPNProvider::CreateService(const string &type,
                                            const string &name,
                                            const string &storage_id,
                                            Error *error) {
  VPNServiceRefPtr service = CreateServiceInner(type, name, storage_id, error);
  if (service) {
    services_.push_back(service);
    manager_->RegisterService(service);
  }

  return service;
}

VPNServiceRefPtr VPNProvider::FindService(const string &type,
                                          const string &name,
                                          const string &host) const {
  for (const auto &service : services_) {
    if (type == service->driver()->GetProviderType() &&
        name == service->friendly_name() &&
        host == service->driver()->GetHost()) {
      return service;
    }
  }
  return nullptr;
}

ServiceRefPtr VPNProvider::CreateTemporaryService(
      const KeyValueStore &args, Error *error) {
  string type;
  string name;
  string host;

  if (!GetServiceParametersFromArgs(args, &type, &name, &host, error)) {
    return nullptr;
  }

  string storage_id = VPNService::CreateStorageIdentifier(args, error);
  if (storage_id.empty()) {
    return nullptr;
  }

  return CreateServiceInner(type, name, storage_id, error);
}

bool VPNProvider::HasActiveService() const {
  for (const auto &service : services_) {
    if (service->IsConnecting() || service->IsConnected()) {
      return true;
    }
  }
  return false;
}

}  // namespace shill
