// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/device.h"

#include <netinet/in.h>
#include <linux/if.h>  // NOLINT - Needs definitions from netinet/in.h
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <time.h>
#include <unistd.h>

#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/containers/contains.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <base/notreached.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <net-base/mac_address.h>
#include <net-base/rtnl_handler.h>

#include "shill/control_interface.h"
#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/network/dhcp_provider.h"
#include "shill/network/legacy_dhcp_controller.h"
#include "shill/network/network.h"
#include "shill/network/network_monitor.h"
#include "shill/refptr_types.h"
#include "shill/service.h"
#include "shill/store/property_accessor.h"
#include "shill/store/store_interface.h"
#include "shill/technology.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDevice;
static std::string ObjectID(const Device* d) {
  return d->GetRpcIdentifier().value();
}
}  // namespace Logging

Device::Device(Manager* manager,
               const std::string& link_name,
               std::optional<net_base::MacAddress> mac_address,
               int interface_index,
               Technology technology,
               bool fixed_ip_params,
               bool use_implicit_network)
    : enabled_(false),
      enabled_persistent_(true),
      enabled_pending_(enabled_),
      mac_address_(mac_address),
      interface_index_(interface_index),
      link_name_(link_name),
      manager_(manager),
      adaptor_(manager->control_interface()->CreateDeviceAdaptor(this)),
      technology_(technology),
      rtnl_handler_(net_base::RTNLHandler::GetInstance()),
      traffic_counter_callback_id_(0),
      weak_ptr_factory_(this) {
  HelpRegisterConstDerivedString(kAddressProperty,
                                 &Device::GetMacAddressString);

  // kBgscanMethodProperty: Registered in WiFi
  // kBgscanShortIntervalProperty: Registered in WiFi
  // kBgscanSignalThresholdProperty: Registered in WiFi

  // kCellularAllowRoamingProperty: Registered in Cellular
  // kEsnProperty: Registered in Cellular
  // kHomeProviderProperty: Registered in Cellular
  // kImeiProperty: Registered in Cellular
  // kIccidProperty: Registered in Cellular
  // kImsiProperty: Registered in Cellular
  // kInhibit: Registered in Cellular
  // kManufacturerProperty: Registered in Cellular
  // kMdnProperty: Registered in Cellular
  // kMeidProperty: Registered in Cellular
  // kMinProperty: Registered in Cellular
  // kModelIdProperty: Registered in Cellular
  // kFirmwareRevisionProperty: Registered in Cellular
  // kHardwareRevisionProperty: Registered in Cellular
  // kDeviceIdProperty: Registered in Cellular
  // kSIMLockStatusProperty: Registered in Cellular
  // kFoundNetworksProperty: Registered in Cellular
  // kDBusObjectProperty: Register in Cellular
  // kPrimaryMultiplexedInterfaceProperty: Registered in Cellular
  // kFlashingProperty: Registered in Cellular

  store_.RegisterConstString(kInterfaceProperty, &link_name_);
  HelpRegisterConstDerivedRpcIdentifier(
      kSelectedServiceProperty, &Device::GetSelectedServiceRpcIdentifier);
  HelpRegisterConstDerivedRpcIdentifiers(kIPConfigsProperty,
                                         &Device::AvailableIPConfigs);
  store_.RegisterConstString(kNameProperty, &link_name_);
  store_.RegisterConstBool(kPoweredProperty, &enabled_);
  HelpRegisterConstDerivedString(kTypeProperty, &Device::GetTechnologyString);

  if (use_implicit_network) {
    CreateImplicitNetwork(fixed_ip_params);
  }

  // kScanningProperty: Registered in WiFi, Cellular
  // kScanIntervalProperty: Registered in WiFi, Cellular
  // kWakeOnWiFiFeaturesEnabledProperty: Registered in WiFi

  SLOG(this, 1) << "Device(): " << link_name_ << " index: " << interface_index_;
}

Device::~Device() {
  LOG(INFO) << "~Device(): " << link_name_ << " index: " << interface_index_;
  if (implicit_network_) {
    implicit_network_->UnregisterEventHandler(this);
  }
}

void Device::CreateImplicitNetwork(bool fixed_ip_params) {
  implicit_network_ = manager_->network_manager()->CreateNetwork(
      interface_index_, link_name_, technology_, fixed_ip_params,
      manager_->patchpanel_client());
  implicit_network_->RegisterEventHandler(this);
}

void Device::Initialize() {
  SLOG(this, 2) << "Initialized";
}

void Device::LinkEvent(unsigned flags, unsigned change) {
  SLOG(this, 2) << base::StringPrintf("Device %s flags 0x%x changed 0x%x",
                                      link_name_.c_str(), flags, change);
}

void Device::Scan(Error* error, const std::string& reason, bool is_dbus_call) {
  SLOG(this, 2) << __func__ << " [Device] on " << link_name() << " from "
                << reason << (is_dbus_call ? " D-Bus call" : "");
  Error::PopulateAndLog(FROM_HERE, error, Error::kNotImplemented,
                        GetTechnologyName() + " device doesn't implement Scan");
}

void Device::RegisterOnNetwork(const std::string& /*network_id*/,
                               ResultCallback callback) {
  Error error;
  Error::PopulateAndLog(
      FROM_HERE, &error, Error::kNotImplemented,
      GetTechnologyName() + " device doesn't implement RegisterOnNetwork");
  std::move(callback).Run(error);
}

void Device::RequirePin(const std::string& /*pin*/,
                        bool /*require*/,
                        ResultCallback callback) {
  Error error;
  Error::PopulateAndLog(
      FROM_HERE, &error, Error::kNotImplemented,
      GetTechnologyName() + " device doesn't implement RequirePin");
  std::move(callback).Run(error);
}

void Device::EnterPin(const std::string& /*pin*/, ResultCallback callback) {
  Error error;
  Error::PopulateAndLog(
      FROM_HERE, &error, Error::kNotImplemented,
      GetTechnologyName() + " device doesn't implement EnterPin");
  std::move(callback).Run(error);
}

void Device::UnblockPin(const std::string& /*unblock_code*/,
                        const std::string& /*pin*/,
                        ResultCallback callback) {
  Error error;
  Error::PopulateAndLog(
      FROM_HERE, &error, Error::kNotImplemented,
      GetTechnologyName() + " device doesn't implement UnblockPin");
  std::move(callback).Run(error);
}

void Device::ChangePin(const std::string& /*old_pin*/,
                       const std::string& /*new_pin*/,
                       ResultCallback callback) {
  Error error;
  Error::PopulateAndLog(
      FROM_HERE, &error, Error::kNotImplemented,
      GetTechnologyName() + " device doesn't implement ChangePin");
  std::move(callback).Run(error);
}

void Device::Reset(ResultCallback callback) {
  Error error;
  Error::PopulateAndLog(
      FROM_HERE, &error, Error::kNotImplemented,
      GetTechnologyName() + " device doesn't implement Reset");
  std::move(callback).Run(error);
}

bool Device::IsConnected() const {
  if (selected_service_)
    return selected_service_->IsConnected();
  return false;
}

void Device::OnSelectedServiceChanged(const ServiceRefPtr&) {}

const RpcIdentifier& Device::GetRpcIdentifier() const {
  return adaptor_->GetRpcIdentifier();
}

std::string Device::GetStorageIdentifier() const {
  return "device_" + DeviceStorageSuffix();
}

void Device::UpdateGeolocationObjects(
    std::vector<GeolocationInfo>* geolocation_infos) const {}

std::string Device::GetTechnologyName() const {
  return TechnologyName(technology());
}

std::string Device::GetTechnologyString(Error* /*error*/) {
  return GetTechnologyName();
}

std::string Device::GetMacAddressHexString() const {
  return mac_address_.has_value() ? mac_address_->ToHexString() : "";
}

std::string Device::GetMacAddressString(Error* /*error*/) {
  return GetMacAddressHexString();
}

const std::string& Device::UniqueName() const {
  return link_name_;
}

Network* Device::GetPrimaryNetwork() const {
  // Return the implicit Network. The implementation of GetPrimaryNetwork() in
  // the base Device class always expects that the implicit Network exists.
  // Subclasses not using the implicit network should provide their own
  // GetPrimaryNetwork() override as well.
  CHECK(implicit_network_);
  return implicit_network_.get();
}

bool Device::IsEventOnPrimaryNetwork(int interface_index) {
  // The interface associated to the primary network may be different than the
  // interface associated to the device when it was created (e.g. for Cellular
  // devices using a multiplexed virtual network interface).
  return (GetPrimaryNetwork() &&
          GetPrimaryNetwork()->interface_index() == interface_index);
}

bool Device::Load(const StoreInterface* storage) {
  const auto id = GetStorageIdentifier();
  if (!storage->ContainsGroup(id)) {
    SLOG(this, 2) << "Device is not available in the persistent store: " << id;
    return false;
  }
  enabled_persistent_ = true;
  storage->GetBool(id, kStoragePowered, &enabled_persistent_);
  return true;
}

bool Device::Save(StoreInterface* storage) {
  const auto id = GetStorageIdentifier();
  storage->SetBool(id, kStoragePowered, enabled_persistent_);
  return true;
}

void Device::OnBeforeSuspend(ResultCallback callback) {
  // Nothing to be done in the general case, so immediately report success.
  std::move(callback).Run(Error(Error::kSuccess));
}

void Device::OnAfterResume() {
  ForceIPConfigUpdate();
}

void Device::OnDarkResume(ResultCallback callback) {
  // Nothing to be done in the general case, so immediately report success.
  std::move(callback).Run(Error(Error::kSuccess));
}

void Device::DropConnection() {
  // The implementation of DropConnection() in the base Device class
  // always stops the implicit network associated to the device.
  // Subclasses not using the implicit network should provide their own
  // DropConnection() override as well.
  SLOG(this, 2) << __func__;
  CHECK(implicit_network_);
  implicit_network_->Stop();
  SelectService(nullptr);
}

void Device::SetUsbEthernetMacAddressSource(const std::string& source,
                                            ResultCallback callback) {
  Error error;
  Error::PopulateAndLog(FROM_HERE, &error, Error::kNotImplemented,
                        "SetUsbEthernetMacAddressSource from source " + source +
                            " is not implemented for " + GetTechnologyName() +
                            " device on " + link_name_ + ".");
  std::move(callback).Run(error);
}

void Device::ForceIPConfigUpdate() {
  SLOG(this, 2) << __func__;
  if (!IsConnected()) {
    return;
  }
  // When already connected, a Network must exist.
  CHECK(GetPrimaryNetwork());
  LOG(INFO) << LoggingTag() << ": forced IP config update";
  GetPrimaryNetwork()->RenewDHCPLease();
  GetPrimaryNetwork()->InvalidateIPv6Config();
}

void Device::FetchTrafficCounters(const ServiceRefPtr& old_service,
                                  const ServiceRefPtr& new_service) {
  patchpanel::Client* client = manager_->patchpanel_client();
  if (!client) {
    return;
  }
  auto primary_network = GetPrimaryNetwork();
  if (!primary_network) {
    return;
  }
  std::set<std::string> devices{primary_network->interface_name()};
  traffic_counter_callback_id_++;
  traffic_counters_callback_map_[traffic_counter_callback_id_] =
      base::BindOnce(&Device::GetTrafficCountersCallback, AsWeakPtr(),
                     old_service, new_service);
  client->GetTrafficCounters(
      devices, base::BindOnce(&Device::GetTrafficCountersPatchpanelCallback,
                              AsWeakPtr(), traffic_counter_callback_id_));
}

void Device::HelpRegisterConstDerivedString(
    std::string_view name, std::string (Device::*get)(Error* error)) {
  store_.RegisterDerivedString(
      name, StringAccessor(
                new CustomAccessor<Device, std::string>(this, get, nullptr)));
}

void Device::HelpRegisterConstDerivedRpcIdentifier(
    std::string_view name, RpcIdentifier (Device::*get)(Error* error)) {
  store_.RegisterDerivedRpcIdentifier(
      name, RpcIdentifierAccessor(
                new CustomAccessor<Device, RpcIdentifier>(this, get, nullptr)));
}

void Device::HelpRegisterConstDerivedRpcIdentifiers(
    std::string_view name, RpcIdentifiers (Device::*get)(Error*)) {
  store_.RegisterDerivedRpcIdentifiers(
      name, RpcIdentifiersAccessor(new CustomAccessor<Device, RpcIdentifiers>(
                this, get, nullptr)));
}

void Device::HelpRegisterConstDerivedUint64(std::string_view name,
                                            uint64_t (Device::*get)(Error*)) {
  store_.RegisterDerivedUint64(
      name,
      Uint64Accessor(new CustomAccessor<Device, uint64_t>(this, get, nullptr)));
}

void Device::OnConnectionUpdated(int interface_index) {
  if (!IsEventOnPrimaryNetwork(interface_index) || !selected_service_) {
    return;
  }

  // If the service is already disconnecting, ignore any update from Network to
  // avoid disrupting the disconnection procedure.
  if (selected_service_->IsDisconnecting()) {
    return;
  }

  // If the service is already in a Connected state (this happens during a roam
  // or DHCP renewal), transitioning back to Connected isn't productive. Avoid
  // this transition entirely and wait for portal detection to transition us to
  // a more informative state (either Online or some portalled state). Instead,
  // set RoamState so that clients that care about the Service's state are still
  // able to track it.
  if (!selected_service_->IsConnected()) {
    // Setting Service.State to Connected resets RoamState.
    SetServiceState(Service::kStateConnected);
  } else {
    // We set RoamState here to reflect the actual state of the Service during a
    // roam. This way, we can keep Service.State at Online or a portalled state
    // to preserve the service sort order. Note that this can be triggered by a
    // DHCP renewal that's not a result of a roam as well, but it won't do
    // anything in non-WiFi Services.
    selected_service_->SetRoamState(Service::kRoamStateConnected);
  }
  OnConnected();

  // If portal detection is disabled for this technology, immediately set
  // the service state to "Online".
  if (selected_service_->GetNetworkValidationMode() ==
      NetworkMonitor::ValidationMode::kDisabled) {
    LOG(INFO) << LoggingTag()
              << ": Portal detection is disabled for this service";
    SetServiceState(Service::kStateOnline);
    return;
  }
}

void Device::OnNetworkStopped(int interface_index, bool is_failure) {
  if (!IsEventOnPrimaryNetwork(interface_index) || !is_failure) {
    return;
  }
  OnIPConfigFailure();
}

void Device::OnIPConfigsPropertyUpdated(int interface_index) {
  if (!IsEventOnPrimaryNetwork(interface_index)) {
    return;
  }
  adaptor_->EmitRpcIdentifierArrayChanged(kIPConfigsProperty,
                                          AvailableIPConfigs(nullptr));
}

void Device::OnIPConfigFailure() {
  if (selected_service_) {
    Error error;
    selected_service_->DisconnectWithFailure(Service::kFailureDHCP, &error,
                                             __func__);
  }
}

void Device::OnConnected() {}

void Device::GetTrafficCountersCallback(
    const ServiceRefPtr& old_service,
    const ServiceRefPtr& new_service,
    const std::vector<patchpanel::Client::TrafficCounter>& counters) {
  if (old_service) {
    old_service->RefreshTrafficCounters(counters);
  }
  if (new_service) {
    // Update the snapshot values, which will be used in future refreshes to
    // diff against the counter values. Snapshot must be initialized before
    // layer 3 configuration to ensure that we capture all traffic for the
    // service.
    new_service->InitializeTrafficCounterSnapshot(counters);
  }
}

void Device::GetTrafficCountersPatchpanelCallback(
    unsigned int id,
    const std::vector<patchpanel::Client::TrafficCounter>& counters) {
  auto iter = traffic_counters_callback_map_.find(id);
  if (iter == traffic_counters_callback_map_.end() || iter->second.is_null()) {
    LOG(ERROR) << LoggingTag() << ": No callback found for ID " << id;
    return;
  }
  if (counters.empty()) {
    LOG(WARNING) << LoggingTag() << ": No counters found";
  }
  auto callback = std::move(iter->second);
  traffic_counters_callback_map_.erase(iter);
  std::move(callback).Run(counters);
}

void Device::SelectService(const ServiceRefPtr& service,
                           bool reset_old_service_state) {
  SLOG(this, 2) << __func__ << ": service "
                << (service ? service->log_name() : "*reset*") << " on "
                << link_name_;

  if (selected_service_.get() == service.get()) {
    // Network may have been previously invalidated, if so, reset.
    if (selected_service_ && !selected_service_->attached_network()) {
      SLOG(this, 2) << __func__ << ": reattaching network to service";
      ResetServiceAttachedNetwork();
    }
    // No change to |selected_service_|. Return early to avoid
    // changing its state.
    return;
  }

  ServiceRefPtr old_service;
  if (selected_service_) {
    old_service = selected_service_;
    if (reset_old_service_state &&
        selected_service_->state() != Service::kStateFailure) {
      SetServiceState(Service::kStateIdle);
    }
    selected_service_->DetachNetwork();
  }

  selected_service_ = service;

  ResetServiceAttachedNetwork();

  OnSelectedServiceChanged(old_service);
  FetchTrafficCounters(old_service, selected_service_);
  adaptor_->EmitRpcIdentifierChanged(kSelectedServiceProperty,
                                     GetSelectedServiceRpcIdentifier(nullptr));
}

void Device::ResetServiceAttachedNetwork() {
  if (selected_service_) {
    auto primary_network = GetPrimaryNetwork();
    CHECK(primary_network);
    selected_service_->AttachNetwork(primary_network->AsWeakPtr());
  }
}

void Device::SetServiceState(Service::ConnectState state) {
  if (selected_service_) {
    selected_service_->SetState(state);
  }
}

void Device::SetServiceFailure(Service::ConnectFailure failure_state) {
  if (selected_service_) {
    selected_service_->SetFailure(failure_state);
  }
}

void Device::SetServiceFailureSilent(Service::ConnectFailure failure_state) {
  if (selected_service_) {
    selected_service_->SetFailureSilent(failure_state);
  }
}

void Device::EmitMACAddress(std::optional<net_base::MacAddress> mac_address) {
  // TODO(b/245984500): What about MAC changed by the supplicant?
  if (mac_address.has_value()) {
    adaptor_->EmitStringChanged(kAddressProperty, mac_address->ToString());
  } else {
    adaptor_->EmitStringChanged(kAddressProperty, GetMacAddressHexString());
  }
}

void Device::set_mac_address(net_base::MacAddress mac_address) {
  mac_address_ = mac_address;
  EmitMACAddress();
}

RpcIdentifier Device::GetSelectedServiceRpcIdentifier(Error* /*error*/) {
  if (!selected_service_) {
    return RpcIdentifier("/");
  }
  return selected_service_->GetRpcIdentifier();
}

RpcIdentifiers Device::AvailableIPConfigs(Error* /*error*/) {
  // These available IPConfigs are the ones exposed in the Device DBus object.
  //
  // The usual case will be a Device object associated to a single given Network
  // where both Device and Network refer to the same network interface in the
  // system; in this case, the IPConfig exposed by the Device applies to the
  // same network interface as the Device references.
  //
  // In other cases, a Device object will have multiple associated Network
  // objects (e.g. Cellular multiplexing), where only one of them is assumed to
  // be "primary". This list will contain the IPConfig of the primary Network
  // exclusively. Also note, this IPConfig for the primary Network may actually
  // refer to a totally different network interface than the one referenced by
  // the Device object, so even if the IPConfig is exposed in DBus by the Device
  // object, it does not mean the IP settings shown in IPConfig will be set in
  // same network interface that the Device references. Ideally IPConfig would
  // also expose the interface name or index in DBus.
  //
  auto primary_network = GetPrimaryNetwork();
  if (!primary_network) {
    return {};
  }
  return primary_network->AvailableIPConfigIdentifiers();
}

bool Device::IsUnderlyingDeviceEnabled() const {
  return false;
}

// callback
void Device::OnEnabledStateChanged(ResultCallback callback,
                                   const Error& error) {
  LOG(INFO) << __func__ << " (target: " << enabled_pending_ << ","
            << " success: " << error.IsSuccess() << ") on " << link_name_;

  if (error.IsSuccess()) {
    UpdateEnabledState();
  } else {
    // Set enabled_pending_ to |enabled_| so that we don't try enabling again
    // after an error.
    enabled_pending_ = enabled_;
  }

  if (!callback.is_null())
    std::move(callback).Run(error);
}

void Device::UpdateEnabledState() {
  SLOG(this, 1) << __func__ << " (current: " << enabled_
                << ", target: " << enabled_pending_ << ") on " << link_name_;
  enabled_ = enabled_pending_;
  if (!enabled_ && ShouldBringNetworkInterfaceDownAfterDisabled()) {
    BringNetworkInterfaceDown();
  }
  manager_->UpdateEnabledTechnologies();
  adaptor_->EmitBoolChanged(kPoweredProperty, enabled_);
}

void Device::OnDeregistered() {}

void Device::SetEnabled(bool enable) {
  LOG(INFO) << __func__ << "(" << enable << ")";
  // TODO(b/172215298): replace DoNothing() with something that logs the error
  // and replace PopulateAndLog in many places with just Populate
  SetEnabledChecked(enable, false, base::DoNothing());
}

void Device::SetEnabledNonPersistent(bool enable, ResultCallback callback) {
  SLOG(this, 1) << __func__ << "(" << enable << ")";
  SetEnabledChecked(enable, false, std::move(callback));
}

void Device::SetEnabledPersistent(bool enable, ResultCallback callback) {
  SLOG(this, 1) << __func__ << "(" << enable << ")";
  SetEnabledChecked(enable, true, std::move(callback));
}

void Device::SetEnabledChecked(bool enable,
                               bool persist,
                               ResultCallback callback) {
  LOG(INFO) << __func__ << ": Device " << link_name_ << " "
            << (enable ? "starting" : "stopping");
  if (enable && manager_->IsTechnologyProhibited(technology())) {
    std::move(callback).Run(
        Error(Error::kPermissionDenied,
              "The " + GetTechnologyName() + " technology is prohibited"));
    return;
  }

  if (enable == enabled_) {
    if (enable != enabled_pending_ && persist) {
      // Return an error, as there is an ongoing operation to achieve the
      // opposite.
      Error err;
      Error::PopulateAndLog(
          FROM_HERE, &err, Error::kOperationFailed,
          enable ? "Cannot enable while the device is disabling."
                 : "Cannot disable while the device is enabling.");
      std::move(callback).Run(err);
      return;
    }
    LOG(INFO) << "Already in desired enable state.";
    // We can already be in the right state, but it may not be persisted.
    // Check and flush that too.
    if (persist && enabled_persistent_ != enable) {
      enabled_persistent_ = enable;
      manager_->UpdateDevice(this);
    }

    if (!callback.is_null())
      std::move(callback).Run(Error(Error::kSuccess));
    return;
  }

  if (enabled_pending_ == enable) {
    Error err;
    Error::PopulateAndLog(FROM_HERE, &err, Error::kInProgress,
                          enable ? "Enable operation already in progress"
                                 : "Disable operation already in progress");
    std::move(callback).Run(err);
    return;
  }

  if (persist) {
    enabled_persistent_ = enable;
    manager_->UpdateDevice(this);
  }

  SetEnabledUnchecked(enable, std::move(callback));
}

void Device::SetEnabledUnchecked(bool enable,
                                 ResultCallback on_enable_complete) {
  LOG(INFO) << LoggingTag() << " SetEnabledUnchecked(" << std::boolalpha
            << enable << ")";
  enabled_pending_ = enable;
  EnabledStateChangedCallback chained_callback =
      base::BindOnce(&Device::OnEnabledStateChanged, AsWeakPtr(),
                     std::move(on_enable_complete));
  if (enable) {
    Start(std::move(chained_callback));
  } else {
    DropConnection();
    if (!ShouldBringNetworkInterfaceDownAfterDisabled()) {
      BringNetworkInterfaceDown();
    }
    Stop(std::move(chained_callback));
  }
}

bool Device::RequestRoam(const std::string& addr, Error* error) {
  return false;
}

bool Device::ShouldBringNetworkInterfaceDownAfterDisabled() const {
  return false;
}

void Device::BringNetworkInterfaceDown() {
  // The implementation of BringNetworkInterfaceDown() in the base Device class
  // always brings down the main network interface associated to the device.
  // Subclasses not using the implicit network should provide their own
  // BringNetworkInterfaceDown() override as well.
  DCHECK(implicit_network_);
  DCHECK(implicit_network_->interface_index() == interface_index());

  // If fixed_ip_params is true, we don't manipulate the interface state.
  if (!implicit_network_->fixed_ip_params())
    rtnl_handler_->SetInterfaceFlags(interface_index(), 0, IFF_UP);
}

ControlInterface* Device::control_interface() const {
  return manager_->control_interface();
}

EventDispatcher* Device::dispatcher() const {
  return manager_->dispatcher();
}

Metrics* Device::metrics() const {
  return manager_->metrics();
}

std::string Device::LoggingTag() const {
  return link_name_ + " " +
         (selected_service_ ? selected_service_->log_name() : "no_service");
}

}  // namespace shill
