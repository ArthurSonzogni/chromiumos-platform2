// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_service.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/net-base/ipv4_address.h>
#include <chromeos/net-base/network_config.h>

#include "shill/dbus/dbus_control.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/profile.h"
#include "shill/static_ip_parameters.h"
#include "shill/store/key_value_store.h"
#include "shill/store/property_accessor.h"
#include "shill/store/store_interface.h"
#include "shill/technology.h"
#include "shill/vpn/vpn_driver.h"
#include "shill/vpn/vpn_metrics.h"
#include "shill/vpn/vpn_provider.h"
#include "shill/vpn/vpn_types.h"
#include "shill/vpn/vpn_util.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
static std::string ObjectID(const VPNService* s) {
  return s->log_name();
}
}  // namespace Logging

namespace {

constexpr uid_t kChronosUid = 1000;

// WireGuardDriver used to use StaticIPConfig to store the local IP address but
// is using a specific property now. This function is for migrating the profile
// data, by the following two actions:
// - Apply the IPv4 address in |static_config| to the WireGuard.IPAddress
//   property in |driver|, if |static_config| has an IPv4 address and the
//   WireGuard.IPAddress property is empty.
// - Reset IPv4 address (with prefix length) in |static_config|.
//
// Returns whether |static_config| is updated.
bool UpdateWireGuardDriverIPv4Address(net_base::NetworkConfig* static_config,
                                      VPNDriver* driver) {
  if (driver->vpn_type() != VPNType::kWireGuard) {
    return false;
  }

  auto static_config_address = static_config->ipv4_address;
  if (!static_config_address) {
    return false;
  }
  // No matter whether the parsing result is valid or not, reset the property.
  static_config->ipv4_address = std::nullopt;

  const auto& current_addrs =
      driver->const_args()->Lookup<std::vector<std::string>>(
          kWireGuardIPAddress, {});
  if (!current_addrs.empty()) {
    return true;
  }

  const std::vector<std::string> addrs_to_set{
      static_config_address->address().ToString()};
  driver->args()->Set<std::vector<std::string>>(kWireGuardIPAddress,
                                                addrs_to_set);
  return true;
}

Service::ConnectFailure VPNEndReasonToServiceFailure(VPNEndReason reason) {
  switch (reason) {
    case VPNEndReason::kDisconnectRequest:
      return Service::kFailureDisconnect;
    case VPNEndReason::kNetworkChange:
      return Service::kFailureConnect;
    case VPNEndReason::kConnectFailureAuthPPP:
      return Service::kFailurePPPAuth;
    case VPNEndReason::kConnectFailureAuthCert:
      // This will be shown as "Authentication certificate rejected by network"
      // in UI.
      return Service::kFailureIPsecCertAuth;
    case VPNEndReason::kConnectFailureAuthUserPassword:
      // This will be shown as "Username/password incorrect or EAP-auth failed"
      // in UI.
      return Service::kFailureEAPAuthentication;
    case VPNEndReason::kConnectFailureDNSLookup:
      return Service::kFailureDNSLookup;
    case VPNEndReason::kConnectTimeout:
      return Service::kFailureConnect;
    case VPNEndReason::kInvalidConfig:
      return Service::kFailureConnect;
    case VPNEndReason::kFailureInternal:
      return Service::kFailureInternal;
    case VPNEndReason::kFailureUnknown:
      return Service::kFailureConnect;
  }
}

bool IsUsedAsDefaultGateway(const net_base::NetworkConfig& config) {
  // If there is no included route, a default route will be installed.
  if (config.included_route_prefixes.empty()) {
    return true;
  }
  // If there is no direct information, infer it from the included routes.
  return VPNUtil::InferIsUsedAsDefaultGatewayFromIncludedRoutes(
      config.included_route_prefixes);
}

// b/328814622: Destroy all Chrome sockets which are bound to the physical
// network to avoid traffic leak.
void DestroyChromeSocketsOnPhysical(VPNDriver* driver,
                                    Service* physical_service,
                                    bool used_as_default_gateway) {
  // Skip if the VPN is a Chrome third-party app, since the socket for the VPN
  // connection itself will also get destroyed in this case.
  if (driver->vpn_type() == VPNType::kThirdParty) {
    LOG(INFO) << __func__ << ": Skip since VPN is a Chrome third-party app";
    return;
  }

  if (!physical_service || !physical_service->attached_network()) {
    LOG(ERROR) << __func__ << ": Skip since default network is empty";
    return;
  }

  // Skip if the VPN is not intentionally used as default gateway, since it may
  // not be expected to destroy them. Ideally we want to do a routing lookup for
  // each socket to decide whether it should be destroyed, but it might be too
  // complicated.
  if (!used_as_default_gateway) {
    LOG(INFO) << __func__ << ": Skip since VPN is split-routing";
    return;
  }

  physical_service->attached_network()->DestroySockets(kChronosUid);
}

}  // namespace

VPNService::VPNService(Manager* manager, std::unique_ptr<VPNDriver> driver)
    : Service(manager, Technology::kVPN), driver_(std::move(driver)) {
  DCHECK(driver_);
  log_name_ = base::StrCat({"vpn_", VPNTypeEnumToString(driver_->vpn_type()),
                            "_", base::NumberToString(serial_number())});
  SetConnectable(true);
  set_save_credentials(false);
  mutable_store()->RegisterDerivedString(
      kPhysicalTechnologyProperty,
      StringAccessor(new CustomAccessor<VPNService, std::string>(
          this, &VPNService::GetPhysicalTechnologyProperty, nullptr)));
  this->manager()->AddDefaultServiceObserver(this);
}

VPNService::~VPNService() {
  manager()->RemoveDefaultServiceObserver(this);
}

void VPNService::OnConnect(Error* error) {
  manager()->vpn_provider()->DisconnectAll();
  // Note that this must be called after VPNProvider::DisconnectAll. While most
  // VPNDrivers create their own Devices, ArcVpnDriver shares the same
  // VirtualDevice (VPNProvider::arc_device), so Disconnect()ing an ARC
  // VPNService after completing the connection for a new ARC VPNService will
  // cause the arc_device to be disabled at the end of this call.

  if (manager()->IsTechnologyProhibited(Technology::kVPN)) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kPermissionDenied,
                          "VPN is prohibited.");
    return;
  }

  SetState(ConnectState::kStateAssociating);
  driver_->driver_metrics()->ReportConnecting();
  // driver_ is owned by VPNService, so this is safe.
  base::TimeDelta timeout = driver_->ConnectAsync(this);
  StartDriverConnectTimeout(timeout);
}

void VPNService::OnDisconnect(Error* error, const char* reason) {
  StopDriverConnectTimeout();
  SetState(ConnectState::kStateDisconnecting);
  driver_->driver_metrics()->ReportDisconnected(
      VPNEndReason::kDisconnectRequest);
  driver_->Disconnect();
  CleanupDevice();

  SetState(ConnectState::kStateIdle);
}

void VPNService::OnDriverConnected(const std::string& if_name, int if_index) {
  StopDriverConnectTimeout();
  if (!CreateDevice(if_name, if_index)) {
    LOG(ERROR) << "Cannot create VPN device for " << if_name;
    SetFailure(Service::kFailureInternal);
    SetErrorDetails(Service::kErrorDetailsNone);
    return;
  }

  // Note that this is the "driver connected" event instead of "network
  // connected", i.e., time to configure network locally won't be included.
  driver_->driver_metrics()->ReportConnected();

  SetState(ConnectState::kStateConfiguring);

  std::unique_ptr<net_base::NetworkConfig> network_config =
      driver_->GetNetworkConfig();

  // This needs to be done before ConfigureDevice() since we will lose the
  // ownership of |network_config| there.
  bool used_as_default_gateway = IsUsedAsDefaultGateway(*network_config);

  ConfigureDevice(std::move(network_config));

  DestroyChromeSocketsOnPhysical(driver(), default_physical_service_.get(),
                                 used_as_default_gateway);

  // Report the final NetworkConfig from the Network object attached to this
  // service. This NetworkConfig should contains all the network config
  // information for this VPN connection (except for the config can be changed
  // after the connection is established, currently this should only be name
  // servers). The assumption here is ConfigureDevice() above will call
  // Network::Start() directly (i.e., without a PostTask()) to finish the setup
  // in Network.
  DCHECK(attached_network());
  driver_->driver_metrics()->ReportNetworkConfig(
      attached_network()->GetNetworkConfig());
}

void VPNService::OnDriverFailure(VPNEndReason failure,
                                 std::string_view error_details) {
  StopDriverConnectTimeout();
  CleanupDevice();
  SetErrorDetails(error_details);
  SetFailure(VPNEndReasonToServiceFailure(failure));
  driver_->driver_metrics()->ReportDisconnected(failure);
}

void VPNService::OnDriverReconnecting(base::TimeDelta timeout) {
  driver_->driver_metrics()->ReportReconnecting();
  StartDriverConnectTimeout(timeout);
  SetState(Service::kStateAssociating);
  // If physical network changes before driver connection finished, this could
  // be called before device_ was initialized.
  if (!device_) {
    return;
  }
  device_->ResetConnection();
}

bool VPNService::CreateDevice(const std::string& if_name, int if_index) {
  // Avoids recreating a VirtualDevice if the network interface is not changed.
  if (device_ != nullptr && device_->link_name() == if_name &&
      device_->interface_index() == if_index) {
    return true;
  }
  // Resets af first to avoid crashing shill in some cases. See
  // b/172228079#comment6.
  device_ = nullptr;
  const bool fixed_ip_params = driver_->vpn_type() == VPNType::kARC;
  device_ = new VirtualDevice(manager(), if_name, if_index, Technology::kVPN,
                              fixed_ip_params);
  return device_ != nullptr;
}

void VPNService::CleanupDevice() {
  if (!device_) {
    return;
  }
  device_->DropConnection();
  device_->SetEnabled(false);
  device_ = nullptr;
}

void VPNService::ConfigureDevice(
    std::unique_ptr<net_base::NetworkConfig> network_config) {
  if (!device_) {
    LOG(DFATAL) << "Device not created yet.";
    return;
  }

  device_->SetEnabled(true);
  device_->SelectService(this);
  device_->UpdateNetworkConfig(std::move(network_config));
}

std::string VPNService::GetStorageIdentifier() const {
  return storage_id_;
}

// static
std::string VPNService::CreateStorageIdentifier(const KeyValueStore& args,
                                                Error* error) {
  const auto host = args.Lookup<std::string>(kProviderHostProperty, "");
  if (host.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidProperty,
                          "Missing VPN host.");
    return "";
  }
  const auto name = args.Lookup<std::string>(kNameProperty, "");
  if (name.empty()) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidProperty,
                          "Missing VPN name.");
    return "";
  }
  return SanitizeStorageIdentifier(
      base::StringPrintf("vpn_%s_%s", host.c_str(), name.c_str()));
}

std::string VPNService::GetPhysicalTechnologyProperty(Error* error) {
  if (!default_physical_service_) {
    error->Populate(Error::kOperationFailed);
    return "";
  }

  return default_physical_service_->GetTechnologyName();
}

RpcIdentifier VPNService::GetDeviceRpcId(Error* error) const {
  if (!device_) {
    error->Populate(Error::kNotFound, "Not associated with a device");
    return DBusControl::NullRpcIdentifier();
  }
  return device_->GetRpcIdentifier();
}

bool VPNService::Load(const StoreInterface* storage) {
  return Service::Load(storage) &&
         driver_->Load(storage, GetStorageIdentifier());
}

void VPNService::MigrateDeprecatedStorage(StoreInterface* storage) {
  Service::MigrateDeprecatedStorage(storage);

  const std::string id = GetStorageIdentifier();
  CHECK(storage->ContainsGroup(id));

  // Can be removed after the next stepping stone version after M114. Note that
  // a VPN service will not be saved automatically if there is no change on
  // values, so we need to trigger a Save() on StaticIPParameters here manually.
  if (UpdateWireGuardDriverIPv4Address(
          mutable_static_ip_parameters()->mutable_config(), driver_.get())) {
    mutable_static_ip_parameters()->Save(storage, id);
  }
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

bool VPNService::SupportsAlwaysOnVpn() {
  // ARC VPNs are not supporting always-on VPN through Shill.
  return driver()->vpn_type() != VPNType::kARC;
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

Service::TetheringState VPNService::GetTethering() const {
  if (!IsConnected()) {
    return TetheringState::kUnknown;
  }
  if (!default_physical_service_) {
    return TetheringState::kUnknown;
  }
  return default_physical_service_->GetTethering();
}

bool VPNService::SetNameProperty(const std::string& name, Error* error) {
  if (name == friendly_name()) {
    return false;
  }
  LOG(INFO) << "SetNameProperty called for: " << log_name();

  KeyValueStore* args = driver_->args();
  args->Set<std::string>(kNameProperty, name);
  const auto new_storage_id = CreateStorageIdentifier(*args, error);
  if (new_storage_id.empty()) {
    return false;
  }
  auto old_storage_id = storage_id_;
  DCHECK_NE(old_storage_id, new_storage_id);

  SetFriendlyName(name);

  // Update the storage identifier before invoking DeleteEntry to prevent it
  // from unloading this service.
  storage_id_ = new_storage_id;
  profile()->DeleteEntry(old_storage_id, nullptr);
  profile()->UpdateService(this);
  return true;
}

VirtualDeviceRefPtr VPNService::GetVirtualDevice() const {
  return device_;
}

void VPNService::OnBeforeSuspend(ResultCallback callback) {
  driver_->OnBeforeSuspend(std::move(callback));
}

void VPNService::OnAfterResume() {
  driver_->OnAfterResume();
  Service::OnAfterResume();
}

void VPNService::OnDefaultPhysicalServiceChanged(
    const ServiceRefPtr& physical_service) {
  SLOG(this, 2) << __func__ << "("
                << (physical_service ? physical_service->log_name() : "-")
                << ")";

  bool default_physical_service_online =
      physical_service && physical_service->IsOnline();
  bool service_changed =
      default_physical_service_.get() != physical_service.get();

  if (!last_default_physical_service_online_ &&
      default_physical_service_online) {
    driver_->OnDefaultPhysicalServiceEvent(
        VPNDriver::DefaultPhysicalServiceEvent::kUp);
  } else if (last_default_physical_service_online_ &&
             !default_physical_service_online) {
    // The default physical service is not online, and nothing else is available
    // right now. All we can do is wait.
    SLOG(this, 2) << __func__ << " - physical service lost or is not online";
    driver_->OnDefaultPhysicalServiceEvent(
        VPNDriver::DefaultPhysicalServiceEvent::kDown);
  } else if (last_default_physical_service_online_ &&
             default_physical_service_online && service_changed) {
    // The original service is no longer the default, but manager was able
    // to find another physical service that is already Online.
    driver_->OnDefaultPhysicalServiceEvent(
        VPNDriver::DefaultPhysicalServiceEvent::kChanged);
  }

  last_default_physical_service_online_ = default_physical_service_online;
  default_physical_service_ =
      physical_service ? physical_service->AsWeakPtr() : nullptr;
}

void VPNService::StartDriverConnectTimeout(base::TimeDelta timeout) {
  if (timeout == VPNDriver::kTimeoutNone) {
    StopDriverConnectTimeout();
    return;
  }
  LOG(INFO) << "Schedule VPN connect timeout: " << timeout.InSeconds()
            << " seconds.";
  driver_connect_timeout_callback_.Reset(BindOnce(
      &VPNService::OnDriverConnectTimeout, weak_factory_.GetWeakPtr()));
  dispatcher()->PostDelayedTask(
      FROM_HERE, driver_connect_timeout_callback_.callback(), timeout);
}

void VPNService::StopDriverConnectTimeout() {
  SLOG(this, 2) << __func__;
  driver_connect_timeout_callback_.Cancel();
}

void VPNService::OnDriverConnectTimeout() {
  LOG(INFO) << "VPN connect timeout.";
  driver_->OnConnectTimeout();
  StopDriverConnectTimeout();
}

}  // namespace shill
