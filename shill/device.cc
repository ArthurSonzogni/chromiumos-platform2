// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/device.h"

#include <errno.h>
#include <netinet/in.h>
#include <linux/if.h>  // NOLINT - Needs definitions from netinet/in.h
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <optional>
#include <set>
#include <string>
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
#include <base/strings/string_piece.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/control_interface.h"
#include "shill/error.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/net/ip_address.h"
#include "shill/net/rtnl_handler.h"
#include "shill/network/dhcp_controller.h"
#include "shill/network/dhcp_provider.h"
#include "shill/network/network.h"
#include "shill/refptr_types.h"
#include "shill/routing_table.h"
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

namespace {

constexpr size_t kHardwareAddressLength = 6;

int PortalResultToMetricsEnum(PortalDetector::Result portal_result) {
  switch (portal_result.http_phase) {
    case PortalDetector::Phase::kUnknown:
      return Metrics::kPortalDetectorResultUnknown;
    case PortalDetector::Phase::kDNS:
      // DNS timeout or failure, portal detection stopped.
      if (portal_result.http_status == PortalDetector::Status::kTimeout) {
        return Metrics::kPortalDetectorResultDNSTimeout;
      } else {
        return Metrics::kPortalDetectorResultDNSFailure;
      }
    case PortalDetector::Phase::kConnection:
      // Connection failed, portal detection stopped.
      return Metrics::kPortalDetectorResultConnectionFailure;
    case PortalDetector::Phase::kHTTP:
      if (portal_result.http_status == PortalDetector::Status::kTimeout) {
        return Metrics::kPortalDetectorResultHTTPTimeout;
      } else {
        return Metrics::kPortalDetectorResultHTTPFailure;
      }
    case PortalDetector::Phase::kContent:
      switch (portal_result.http_status) {
        case PortalDetector::Status::kFailure:
          return Metrics::kPortalDetectorResultContentFailure;
        case PortalDetector::Status::kSuccess:
          if (portal_result.https_status == PortalDetector::Status::kSuccess) {
            return Metrics::kPortalDetectorResultOnline;
          } else {
            return Metrics::kPortalDetectorResultHTTPSFailure;
          }
        case PortalDetector::Status::kTimeout:
          if (portal_result.https_status == PortalDetector::Status::kSuccess) {
            // The HTTP probe timed out but the HTTPS probe succeeded.
            // We expect this to be an uncommon edge case.
            return Metrics::kPortalDetectorResultContentTimeout;
          } else {
            return Metrics::kPortalDetectorResultNoConnectivity;
          }
        case PortalDetector::Status::kRedirect:
          if (!portal_result.redirect_url_string.empty()) {
            return Metrics::kPortalDetectorResultRedirectFound;
          } else {
            return Metrics::kPortalDetectorResultRedirectNoUrl;
          }
      }
  }
}

Service::ConnectState PortalValidationStateToConnectionState(
    PortalDetector::ValidationState validation_state) {
  switch (validation_state) {
    case PortalDetector::ValidationState::kInternetConnectivity:
      return Service::kStateOnline;
    case PortalDetector::ValidationState::kNoConnectivity:
      return Service::kStateNoConnectivity;
    case PortalDetector::ValidationState::kPartialConnectivity:
      return Service::kStatePortalSuspected;
    case PortalDetector::ValidationState::kPortalRedirect:
      return Service::kStateRedirectFound;
  }
}

}  // namespace

const char Device::kStoragePowered[] = "Powered";

Device::Device(Manager* manager,
               const std::string& link_name,
               const std::string& mac_address,
               int interface_index,
               Technology technology,
               bool fixed_ip_params)
    : enabled_(false),
      enabled_persistent_(true),
      enabled_pending_(enabled_),
      mac_address_(base::ToLowerASCII(mac_address)),
      interface_index_(interface_index),
      link_name_(link_name),
      manager_(manager),
      network_(new Network(interface_index,
                           link_name,
                           technology,
                           fixed_ip_params,
                           manager->control_interface(),
                           manager->dispatcher(),
                           manager->metrics())),
      adaptor_(manager->control_interface()->CreateDeviceAdaptor(this)),
      technology_(technology),
      rtnl_handler_(RTNLHandler::GetInstance()),
      traffic_counter_callback_id_(0),
      weak_ptr_factory_(this) {
  store_.RegisterConstString(kAddressProperty, &mac_address_);

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

  store_.RegisterConstString(kInterfaceProperty, &link_name_);
  HelpRegisterConstDerivedRpcIdentifier(
      kSelectedServiceProperty, &Device::GetSelectedServiceRpcIdentifier);
  HelpRegisterConstDerivedRpcIdentifiers(kIPConfigsProperty,
                                         &Device::AvailableIPConfigs);
  store_.RegisterConstString(kNameProperty, &link_name_);
  store_.RegisterConstBool(kPoweredProperty, &enabled_);
  HelpRegisterConstDerivedString(kTypeProperty, &Device::GetTechnologyString);

  network_->RegisterEventHandler(this);

  // kScanningProperty: Registered in WiFi, Cellular
  // kScanIntervalProperty: Registered in WiFi, Cellular
  // kWakeOnWiFiFeaturesEnabledProperty: Registered in WiFi

  SLOG(this, 1) << "Device(): " << link_name_ << " index: " << interface_index_;
}

Device::~Device() {
  LOG(INFO) << "~Device(): " << link_name_ << " index: " << interface_index_;
  network_->UnregisterEventHandler(this);
}

void Device::Initialize() {
  SLOG(this, 2) << "Initialized";
}

void Device::LinkEvent(unsigned flags, unsigned change) {
  SLOG(this, 2) << base::StringPrintf("Device %s flags 0x%x changed 0x%x",
                                      link_name_.c_str(), flags, change);
}

void Device::Scan(Error* error, const std::string& reason) {
  SLOG(this, 2) << __func__ << " [Device] on " << link_name() << " from "
                << reason;
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

bool Device::IsConnectedToService(const ServiceRefPtr& service) const {
  return service == selected_service_ && IsConnected();
}

void Device::OnSelectedServiceChanged(const ServiceRefPtr&) {}

const RpcIdentifier& Device::GetRpcIdentifier() const {
  return adaptor_->GetRpcIdentifier();
}

std::string Device::GetStorageIdentifier() const {
  return "device_" + mac_address_;
}

void Device::UpdateGeolocationObjects(
    std::vector<GeolocationInfo>* geolocation_infos) const {}

std::string Device::GetTechnologyName() const {
  return TechnologyName(technology());
}

std::string Device::GetTechnologyString(Error* /*error*/) {
  return GetTechnologyName();
}

const std::string& Device::UniqueName() const {
  return link_name_;
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
  SLOG(this, 2) << __func__;
  network_->Stop();
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
  LOG(INFO) << LoggingTag() << ": " << __func__;
  network()->RenewDHCPLease();
  network()->InvalidateIPv6Config();
}

void Device::FetchTrafficCounters(const ServiceRefPtr& old_service,
                                  const ServiceRefPtr& new_service) {
  std::set<std::string> devices{link_name_};
  patchpanel::Client* client = manager_->patchpanel_client();
  if (!client) {
    return;
  }
  traffic_counter_callback_id_++;
  traffic_counters_callback_map_[traffic_counter_callback_id_] =
      base::BindOnce(&Device::GetTrafficCountersCallback, AsWeakPtr(),
                     old_service, new_service);
  client->GetTrafficCounters(
      devices, base::BindOnce(&Device::GetTrafficCountersPatchpanelCallback,
                              AsWeakPtr(), traffic_counter_callback_id_));
}

void Device::OnNeighborReachabilityEvent(
    int interface_index,
    const IPAddress& ip_address,
    patchpanel::Client::NeighborRole role,
    patchpanel::Client::NeighborStatus status) {
  // Does nothing in the general case.
}

void Device::HelpRegisterConstDerivedString(
    base::StringPiece name, std::string (Device::*get)(Error* error)) {
  store_.RegisterDerivedString(
      name, StringAccessor(
                new CustomAccessor<Device, std::string>(this, get, nullptr)));
}

void Device::HelpRegisterConstDerivedRpcIdentifier(
    base::StringPiece name, RpcIdentifier (Device::*get)(Error* error)) {
  store_.RegisterDerivedRpcIdentifier(
      name, RpcIdentifierAccessor(
                new CustomAccessor<Device, RpcIdentifier>(this, get, nullptr)));
}

void Device::HelpRegisterConstDerivedRpcIdentifiers(
    base::StringPiece name, RpcIdentifiers (Device::*get)(Error*)) {
  store_.RegisterDerivedRpcIdentifiers(
      name, RpcIdentifiersAccessor(new CustomAccessor<Device, RpcIdentifiers>(
                this, get, nullptr)));
}

void Device::HelpRegisterConstDerivedUint64(base::StringPiece name,
                                            uint64_t (Device::*get)(Error*)) {
  store_.RegisterDerivedUint64(
      name,
      Uint64Accessor(new CustomAccessor<Device, uint64_t>(this, get, nullptr)));
}

void Device::OnConnectionUpdated(int interface_index) {
  DCHECK(interface_index == interface_index_);

  if (!selected_service_) {
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

  // Subtle: Start portal detection after transitioning the service to the
  // Connected state because this call may immediately transition to the Online
  // state. Always ignore any on-going portal detection such that the latest
  // network layer properties are used to restart portal detection. This ensures
  // that network validation over IPv4 is prioritized on dual stack networks
  // when IPv4 provisioning completes after IPv6 provisioning. Note that
  // currently SetupConnection() is never called a second time if IPv6
  // provisioning completes after IPv4 provisioning.
  UpdatePortalDetector(/*restart=*/true);
}

void Device::OnNetworkStopped(int interface_index, bool is_failure) {
  DCHECK(interface_index == interface_index_);
  if (is_failure) {
    OnIPConfigFailure();
  }
}

void Device::OnGetDHCPLease(int interface_index) {}
void Device::OnGetDHCPFailure(int interface_index) {}
void Device::OnGetSLAACAddress(int interface_index) {}
void Device::OnNetworkValidationStart(int interface_index) {}
void Device::OnNetworkValidationStop(int interface_index) {}
void Device::OnNetworkValidationSuccess() {}
void Device::OnNetworkValidationFailure() {}
void Device::OnIPv4ConfiguredWithDHCPLease(int interface_index) {}
void Device::OnIPv6ConfiguredWithSLAACAddress(int interface_index) {}
void Device::OnNetworkDestroyed(int interface_index) {}

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
    selected_service_->SetAttachedNetwork(nullptr);
  }

  selected_service_ = service;
  network_->set_logging_tag(LoggingTag());
  if (selected_service_) {
    selected_service_->SetAttachedNetwork(network_->AsWeakPtr());
  }
  OnSelectedServiceChanged(old_service);
  FetchTrafficCounters(old_service, selected_service_);
  adaptor_->EmitRpcIdentifierChanged(kSelectedServiceProperty,
                                     GetSelectedServiceRpcIdentifier(nullptr));
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

bool Device::UpdatePortalDetector(bool restart) {
  SLOG(this, 1) << LoggingTag() << ": " << __func__ << " restart=" << restart;

  if (!selected_service_) {
    LOG(INFO) << LoggingTag() << ": Skipping portal detection: no Service";
    return false;
  }

  // Do not run portal detection unless in a connected state (i.e. connected,
  // online, or portalled).
  if (!selected_service_->IsConnected()) {
    LOG(INFO) << LoggingTag()
              << ": Skipping portal detection: Service is not connected";
    return false;
  }

  // If portal detection is disabled for this technology, immediately set
  // the service state to "Online" and stop portal detection if it was
  // running.
  if (selected_service_->IsPortalDetectionDisabled()) {
    LOG(INFO) << LoggingTag()
              << ": Portal detection is disabled for this service";
    network_->StopPortalDetection();
    SetServiceState(Service::kStateOnline);
    return false;
  }

  if (!network_->StartPortalDetection(restart)) {
    SetServiceState(Service::kStateOnline);
    return false;
  }

  return true;
}

void Device::EmitMACAddress(const std::string& mac_address) {
  // TODO(b/245984500): What about MAC changed by the supplicant?
  if (mac_address.empty() ||
      MakeHardwareAddressFromString(mac_address).empty()) {
    adaptor_->EmitStringChanged(kAddressProperty, mac_address_);
  } else {
    adaptor_->EmitStringChanged(kAddressProperty, mac_address);
  }
}

void Device::set_mac_address(const std::string& mac_address) {
  mac_address_ = mac_address;
  EmitMACAddress();
}

void Device::OnNetworkValidationResult(int interface_index,
                                       const PortalDetector::Result& result) {
  DCHECK(interface_index == interface_index_);

  if (!selected_service_) {
    // A race can happen if the Service has disconnected in the meantime.
    LOG(WARNING)
        << LoggingTag() << ": "
        << "Portal detection completed but no selected service exists.";
    return;
  }

  if (!selected_service_->IsConnected()) {
    // A race can happen if the Service is currently disconnecting.
    LOG(WARNING) << LoggingTag() << ": "
                 << "Portal detection completed but selected service is in "
                    "non-connected state.";
    return;
  }

  selected_service_->increment_portal_detection_count();
  int portal_detection_count = selected_service_->portal_detection_count();
  int portal_result = PortalResultToMetricsEnum(result);
  metrics()->SendEnumToUMA(portal_detection_count == 1
                               ? Metrics::kPortalDetectorInitialResult
                               : Metrics::kPortalDetectorRetryResult,
                           technology(), portal_result);

  // Set the probe URL. It should be empty if there is no redirect.
  selected_service_->SetProbeUrl(result.probe_url_string);

  Service::ConnectState state =
      PortalValidationStateToConnectionState(result.GetValidationState());
  if (state == Service::kStateOnline) {
    OnNetworkValidationSuccess();
    // TODO(b/248028325) Move StopPortalDetection inside Network and only
    // process the new ConnectState in OnNetworkValidationResult.
    network_->StopPortalDetection();
  } else if (Service::IsPortalledState(state)) {
    OnNetworkValidationFailure();
    selected_service_->SetPortalDetectionFailure(
        PortalDetector::PhaseToString(result.http_phase),
        PortalDetector::StatusToString(result.http_status),
        result.http_status_code);
    if (!network_->RestartPortalDetection()) {
      state = Service::kStateOnline;
    }
  } else {
    // TODO(b/248028325) Use PortalDetector::ValidationState directly to avoid
    // this branch at compile time.
    LOG(ERROR) << LoggingTag() << ": unexpected Service state " << state
               << " from portal detection result";
    state = Service::kStateOnline;
    network_->StopPortalDetection();
  }

  SetServiceState(state);
}

RpcIdentifier Device::GetSelectedServiceRpcIdentifier(Error* /*error*/) {
  if (!selected_service_) {
    return RpcIdentifier("/");
  }
  return selected_service_->GetRpcIdentifier();
}

RpcIdentifiers Device::AvailableIPConfigs(Error* /*error*/) {
  RpcIdentifiers identifiers;
  if (network_->ipconfig()) {
    identifiers.push_back(network_->ipconfig()->GetRpcIdentifier());
  }
  if (network_->ip6config()) {
    identifiers.push_back(network_->ip6config()->GetRpcIdentifier());
  }
  return identifiers;
}

bool Device::IsUnderlyingDeviceEnabled() const {
  return false;
}

// callback
void Device::OnEnabledStateChanged(ResultCallback callback,
                                   const Error& error) {
  LOG(INFO) << __func__ << " (target: " << enabled_pending_ << ","
            << " success: " << error.IsSuccess() << ")"
            << " on " << link_name_;

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
                << ", target: " << enabled_pending_ << ")"
                << " on " << link_name_;
  enabled_ = enabled_pending_;
  if (!enabled_ && ShouldBringNetworkInterfaceDownAfterDisabled()) {
    BringNetworkInterfaceDown();
  }
  manager_->UpdateEnabledTechnologies();
  adaptor_->EmitBoolChanged(kPoweredProperty, enabled_);
}

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
                          "Enable operation already in progress");
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

void Device::OnIPConfigsPropertyUpdated(int interface_index) {
  DCHECK(interface_index == interface_index_);
  adaptor_->EmitRpcIdentifierArrayChanged(kIPConfigsProperty,
                                          AvailableIPConfigs(nullptr));
}

// static
std::vector<uint8_t> Device::MakeHardwareAddressFromString(
    const std::string& address_string) {
  std::string address_nosep;
  base::RemoveChars(address_string, ":", &address_nosep);
  std::vector<uint8_t> address_bytes;
  base::HexStringToBytes(address_nosep, &address_bytes);
  if (address_bytes.size() != kHardwareAddressLength) {
    return std::vector<uint8_t>();
  }
  return address_bytes;
}

// static
std::string Device::MakeStringFromHardwareAddress(
    const std::vector<uint8_t>& address_bytes) {
  CHECK_EQ(kHardwareAddressLength, address_bytes.size());
  return base::StringPrintf(
      "%02x:%02x:%02x:%02x:%02x:%02x", address_bytes[0], address_bytes[1],
      address_bytes[2], address_bytes[3], address_bytes[4], address_bytes[5]);
}

bool Device::RequestRoam(const std::string& addr, Error* error) {
  return false;
}

bool Device::ShouldBringNetworkInterfaceDownAfterDisabled() const {
  return false;
}

void Device::BringNetworkInterfaceDown() {
  // If fixed_ip_params is true, we don't manipulate the interface state.
  if (!network_->fixed_ip_params())
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
