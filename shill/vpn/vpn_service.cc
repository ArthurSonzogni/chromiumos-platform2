// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_service.h"

#include <algorithm>
#include <utility>

#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/connection.h"
#include "shill/key_value_store.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/profile.h"
#include "shill/property_accessor.h"
#include "shill/technology.h"
#include "shill/vpn/vpn_driver.h"
#include "shill/vpn/vpn_provider.h"

using base::StringPrintf;
using std::string;

namespace shill {

const char VPNService::kAutoConnNeverConnected[] = "never connected";
const char VPNService::kAutoConnVPNAlreadyActive[] = "vpn already active";

VPNService::VPNService(Manager* manager, std::unique_ptr<VPNDriver> driver)
    : Service(manager, Technology::kVPN), driver_(std::move(driver)) {
  SetConnectable(true);
  set_save_credentials(false);
  mutable_store()->RegisterDerivedString(
      kPhysicalTechnologyProperty,
      StringAccessor(new CustomAccessor<VPNService, string>(
          this, &VPNService::GetPhysicalTechnologyProperty, nullptr)));
}

VPNService::~VPNService() = default;

void VPNService::OnConnect(Error* error) {
  manager()->vpn_provider()->DisconnectAll();
  // Note that this must be called after VPNProvider::DisconnectAll. While most
  // VPNDrivers create their own Devices, ArcVpnDriver shares the same
  // VirtualDevice (VPNProvider::arc_device), so Disconnect()ing an ARC
  // VPNService after completing the connection for a new ARC VPNService will
  // cause the arc_device to be disabled at the end of this call.
  driver_->Connect(this, error);
}

void VPNService::OnDisconnect(Error* error, const char* reason) {
  driver_->Disconnect();
}

string VPNService::GetStorageIdentifier() const {
  return storage_id_;
}

bool VPNService::IsAlwaysOnVpn(const string& package) const {
  // For ArcVPN connections, the driver host is set to the package name of the
  // Android app that is creating the VPN connection.
  return driver_->GetProviderType() == string(kProviderArcVpn) &&
         driver_->GetHost() == package;
}

// static
string VPNService::CreateStorageIdentifier(const KeyValueStore& args,
                                           Error* error) {
  string host = args.LookupString(kProviderHostProperty, "");
  if (host.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidProperty,
                          "Missing VPN host.");
    return "";
  }
  string name = args.LookupString(kNameProperty, "");
  if (name.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kNotSupported,
                          "Missing VPN name.");
    return "";
  }
  return SanitizeStorageIdentifier(
      StringPrintf("vpn_%s_%s", host.c_str(), name.c_str()));
}

string VPNService::GetPhysicalTechnologyProperty(Error* error) {
  ConnectionConstRefPtr underlying_connection = GetUnderlyingConnection();
  if (!underlying_connection) {
    error->Populate(Error::kOperationFailed);
    return "";
  }

  return underlying_connection->technology().GetName();
}

RpcIdentifier VPNService::GetDeviceRpcId(Error* error) const {
  error->Populate(Error::kNotSupported);
  return RpcIdentifier("/");
}

ConnectionConstRefPtr VPNService::GetUnderlyingConnection() const {
  // TODO(crbug.com/941597) Policy routing should be used to enforce that VPN
  // traffic can only exit the interface it is supposed to. The VPN driver
  // should also be informed of changes in underlying connection.
  ServiceRefPtr underlying_service = manager()->GetPrimaryPhysicalService();
  if (!underlying_service) {
    return nullptr;
  }
  return underlying_service->connection();
}

bool VPNService::Load(StoreInterface* storage) {
  return Service::Load(storage) &&
         driver_->Load(storage, GetStorageIdentifier());
}

bool VPNService::Save(StoreInterface* storage) {
  return Service::Save(storage) &&
         driver_->Save(storage, GetStorageIdentifier(), save_credentials());
}

bool VPNService::Unload() {
  // The base method also disconnects the service.
  Service::Unload();

  set_save_credentials(false);
  driver_->UnloadCredentials();

  // Ask the VPN provider to remove us from its list.
  manager()->vpn_provider()->RemoveService(this);

  return true;
}

void VPNService::InitDriverPropertyStore() {
  driver_->InitPropertyStore(mutable_store());
}

void VPNService::EnableAndRetainAutoConnect() {
  // The base EnableAndRetainAutoConnect method also sets auto_connect_ to true
  // which is not desirable for VPN services.
  RetainAutoConnect();
}

bool VPNService::IsAutoConnectable(const char** reason) const {
  if (!Service::IsAutoConnectable(reason)) {
    return false;
  }
  // Don't auto-connect VPN services that have never connected. This improves
  // the chances that the VPN service is connectable and avoids dialog popups.
  if (!has_ever_connected()) {
    *reason = kAutoConnNeverConnected;
    return false;
  }
  // Don't auto-connect a VPN service if another VPN service is already active.
  if (manager()->vpn_provider()->HasActiveService()) {
    *reason = kAutoConnVPNAlreadyActive;
    return false;
  }
  return true;
}

string VPNService::GetTethering(Error* error) const {
  ConnectionConstRefPtr underlying_connection = GetUnderlyingConnection();
  string tethering;
  if (underlying_connection) {
    tethering = underlying_connection->tethering();
    if (!tethering.empty()) {
      return tethering;
    }
    // The underlying service may not have a Tethering property.  This is
    // not strictly an error, so we don't print an error message.  Populating
    // an error here just serves to propagate the lack of a property in
    // GetProperties().
    error->Populate(Error::kNotSupported);
  } else {
    error->Populate(Error::kOperationFailed);
  }
  return "";
}

bool VPNService::SetNameProperty(const string& name, Error* error) {
  if (name == friendly_name()) {
    return false;
  }
  LOG(INFO) << "Renaming service " << unique_name() << ": " << friendly_name()
            << " -> " << name;

  KeyValueStore* args = driver_->args();
  args->SetString(kNameProperty, name);
  string new_storage_id = CreateStorageIdentifier(*args, error);
  if (new_storage_id.empty()) {
    return false;
  }
  string old_storage_id = storage_id_;
  DCHECK_NE(old_storage_id, new_storage_id);

  SetFriendlyName(name);

  // Update the storage identifier before invoking DeleteEntry to prevent it
  // from unloading this service.
  storage_id_ = new_storage_id;
  profile()->DeleteEntry(old_storage_id, nullptr);
  profile()->UpdateService(this);
  return true;
}

void VPNService::OnBeforeSuspend(const ResultCallback& callback) {
  driver_->OnBeforeSuspend(callback);
}

void VPNService::OnAfterResume() {
  driver_->OnAfterResume();
  Service::OnAfterResume();
}

void VPNService::OnDefaultServiceStateChanged(const ServiceRefPtr& service) {
  driver_->OnDefaultServiceStateChanged(service);
}

}  // namespace shill
