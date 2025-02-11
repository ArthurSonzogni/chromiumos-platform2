// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/service.h"

#include <stdio.h>

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
#include <base/containers/contains.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <base/types/cxx23_to_underlying.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/dbus/shill/dbus-constants.h>
#include <chromeos/patchpanel/dbus/client.h>
#include <metrics/bootstat.h>
#include <metrics/timer.h>

#include "shill/cellular/power_opt.h"
#include "shill/dbus/dbus_control.h"
#include "shill/eap_credentials.h"
#include "shill/error.h"
#include "shill/event_history.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/metrics.h"
#include "shill/network/network.h"
#include "shill/network/network_monitor.h"
#include "shill/network/portal_detector.h"
#include "shill/profile.h"
#include "shill/refptr_types.h"
#include "shill/store/pkcs11_slot_getter.h"
#include "shill/store/property_accessor.h"
#include "shill/store/store_interface.h"

namespace shill {

namespace {
constexpr char kServiceSortAutoConnect[] = "AutoConnect";
constexpr char kServiceSortConnectable[] = "Connectable";
constexpr char kServiceSortHasEverConnected[] = "HasEverConnected";
constexpr char kServiceSortManagedCredentials[] = "ManagedCredentials";
constexpr char kServiceSortIsConnected[] = "IsConnected";
constexpr char kServiceSortIsConnecting[] = "IsConnecting";
constexpr char kServiceSortIsFailed[] = "IsFailed";
constexpr char kServiceSortIsOnline[] = "IsOnline";
constexpr char kServiceSortIsPortalled[] = "IsPortal";
constexpr char kServiceSortPriority[] = "Priority";
constexpr char kServiceSortSecurity[] = "Security";
constexpr char kServiceSortSource[] = "Source";
constexpr char kServiceSortProfileOrder[] = "ProfileOrder";
constexpr char kServiceSortEtc[] = "Etc";
constexpr char kServiceSortSerialNumber[] = "SerialNumber";
constexpr char kServiceSortTechnology[] = "Technology";
constexpr char kServiceSortTechnologySpecific[] = "TechnologySpecific";

constexpr char kStorageDeprecatedLinkMonitorDisabled[] = "LinkMonitorDisabled";

// This is property is only supposed to be used in tast tests to order Ethernet
// services. Can be removed once we support multiple Ethernet profiles properly
// (b/159725895).
constexpr char kEphemeralPriorityProperty[] = "EphemeralPriority";

// JSON keys and values for Service property ProxyConfig. Must be kept
// consistent with chromium/src/components/proxy_config/proxy_prefs.cc and
// shill/doc/service_api.txt.
constexpr char kServiceProxyConfigMode[] = "mode";
constexpr char kServiceProxyConfigModeDirect[] = "direct";

constexpr int kPriorityNone = 0;

constexpr base::TimeDelta kMinAutoConnectCooldownTime = base::Seconds(1);
constexpr base::TimeDelta kMaxAutoConnectCooldownTime = base::Minutes(1);

// This is the mapping of ONC enum values and their textual representation.
static constexpr std::
    array<const char*, base::to_underlying(Service::ONCSource::kONCSourcesNum)>
        kONCSourceMapping = {kONCSourceUnknown, kONCSourceNone,
                             kONCSourceUserImport, kONCSourceDevicePolicy,
                             kONCSourceUserPolicy};

// Get JSON value from |json| dictionary keyed by |key|.
std::optional<std::string> GetJSONDictValue(std::string_view json,
                                            std::string_view key) {
  auto dict = base::JSONReader::ReadDict(json);
  if (!dict) {
    return std::nullopt;
  }
  auto val = dict->FindString(key);
  if (!val) {
    return std::nullopt;
  }
  return *val;
}

}  // namespace

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kService;
}  // namespace Logging

// static
unsigned int Service::next_serial_number_ = 0;

// static
std::string Service::CheckPortalStateToString(CheckPortalState state) {
  switch (state) {
    case CheckPortalState::kTrue:
      return kCheckPortalTrue;
    case CheckPortalState::kFalse:
      return kCheckPortalFalse;
    case CheckPortalState::kHTTPOnly:
      return kCheckPortalHTTPOnly;
    case CheckPortalState::kAutomatic:
      return kCheckPortalAuto;
  }
}

// static
std::optional<Service::CheckPortalState> Service::CheckPortalStateFromString(
    std::string_view state_name) {
  if (state_name == kCheckPortalTrue) {
    return CheckPortalState::kTrue;
  } else if (state_name == kCheckPortalFalse) {
    return CheckPortalState::kFalse;
  } else if (state_name == kCheckPortalHTTPOnly) {
    return CheckPortalState::kHTTPOnly;
  } else if (state_name == kCheckPortalAuto) {
    return CheckPortalState::kAutomatic;
  }
  return std::nullopt;
}

Service::Service(Manager* manager, Technology technology)
    : weak_ptr_factory_(this),
      state_(kStateIdle),
      previous_state_(kStateIdle),
      failure_(kFailureNone),
      auto_connect_(false),
      retain_auto_connect_(false),
      was_visible_(false),
      check_portal_(CheckPortalState::kAutomatic),
      connectable_(false),
      error_(ConnectFailureToString(failure_)),
      error_details_(kErrorDetailsNone),
      previous_error_serial_number_(0),
      explicitly_disconnected_(false),
      is_in_user_connect_(false),
      is_in_auto_connect_(false),
      priority_(kPriorityNone),
      crypto_algorithm_(kCryptoNone),
      key_rotation_(false),
      endpoint_auth_(false),
      strength_(0),
      save_credentials_(true),
      technology_(technology),
      has_ever_connected_(false),
      disconnects_(kMaxDisconnectEventHistory),
      misconnects_(kMaxMisconnectEventHistory),
      store_(base::BindRepeating(&Service::OnPropertyChanged,
                                 weak_ptr_factory_.GetWeakPtr())),
      serial_number_(next_serial_number_++),
      network_event_handler_(std::make_unique<NetworkEventHandler>(this)),
      adaptor_(manager->control_interface()->CreateServiceAdaptor(this)),
      manager_(manager),
      link_monitor_disabled_(false),
      managed_credentials_(false),
      unreliable_(false),
      source_(ONCSource::kONCSourceUnknown),
      time_resume_to_ready_timer_(new chromeos_metrics::Timer),
      ca_cert_experiment_phase_(
          EapCredentials::CaCertExperimentPhase::kDisabled) {
  // Provide a default name.
  friendly_name_ = "service_" + base::NumberToString(serial_number_);
  log_name_ = friendly_name_;

  HelpRegisterDerivedBool(kAutoConnectProperty, &Service::GetAutoConnect,
                          &Service::SetAutoConnectFull,
                          &Service::ClearAutoConnect);

  // kActivationTypeProperty: Registered in CellularService
  // kActivationStateProperty: Registered in CellularService
  // kCellularApnProperty: Registered in CellularService
  // kCellularLastGoodApnProperty: Registered in CellularService
  // kNetworkTechnologyProperty: Registered in CellularService
  // kOutOfCreditsProperty: Registered in CellularService
  // kPaymentPortalProperty: Registered in CellularService
  // kRoamingStateProperty: Registered in CellularService
  // kServingOperatorProperty: Registered in CellularService
  // kUsageURLProperty: Registered in CellularService
  // kCellularPPPUsernameProperty: Registered in CellularService
  // kCellularPPPPasswordProperty: Registered in CellularService

  HelpRegisterDerivedString(kCheckPortalProperty, &Service::GetCheckPortal,
                            &Service::SetCheckPortal);
  store_.RegisterConstBool(kConnectableProperty, &connectable_);
  HelpRegisterConstDerivedRpcIdentifier(kDeviceProperty,
                                        &Service::GetDeviceRpcId);
  store_.RegisterConstStrings(kEapRemoteCertificationProperty,
                              &remote_certification_);
  HelpRegisterDerivedString(kGuidProperty, &Service::GetGuid,
                            &Service::SetGuid);

  // TODO(ers): in flimflam clearing Error has the side-effect of
  // setting the service state to IDLE. Is this important? I could
  // see an autotest depending on it.
  store_.RegisterConstString(kErrorProperty, &error_);
  store_.RegisterConstString(kErrorDetailsProperty, &error_details_);
  HelpRegisterConstDerivedRpcIdentifier(kIPConfigProperty,
                                        &Service::GetIPConfigRpcIdentifier);
  store_.RegisterDerivedBool(
      kIsConnectedProperty,
      BoolAccessor(new CustomReadOnlyAccessor<Service, bool>(
          this, &Service::IsConnected)));
  // kModeProperty: Registered in WiFiService

  HelpRegisterDerivedString(kNameProperty, &Service::GetNameProperty,
                            &Service::SetNameProperty);
  store_.RegisterConstString(kLogNameProperty, &log_name_);
  // kPassphraseProperty: Registered in WiFiService
  // kPassphraseRequiredProperty: Registered in WiFiService
  store_.RegisterConstString(kPreviousErrorProperty, &previous_error_);
  store_.RegisterConstInt32(kPreviousErrorSerialNumberProperty,
                            &previous_error_serial_number_);
  HelpRegisterDerivedInt32(kPriorityProperty, &Service::GetPriority,
                           &Service::SetPriority);
  store_.RegisterInt32(kEphemeralPriorityProperty, &ephemeral_priority_);
  HelpRegisterDerivedString(kProfileProperty, &Service::GetProfileRpcId,
                            &Service::SetProfileRpcId);
  HelpRegisterDerivedString(kProxyConfigProperty, &Service::GetProxyConfig,
                            &Service::SetProxyConfig);
  store_.RegisterBool(kSaveCredentialsProperty, &save_credentials_);
  HelpRegisterDerivedString(kTypeProperty, &Service::CalculateTechnology,
                            nullptr);
  // kSecurityProperty: Registered in WiFiService
  HelpRegisterDerivedString(kStateProperty, &Service::CalculateState, nullptr);
  store_.RegisterConstUint8(kSignalStrengthProperty, &strength_);
  store_.RegisterString(kUIDataProperty, &ui_data_);
  HelpRegisterConstDerivedStrings(kDiagnosticsDisconnectsProperty,
                                  &Service::GetDisconnectsProperty);
  HelpRegisterConstDerivedStrings(kDiagnosticsMisconnectsProperty,
                                  &Service::GetMisconnectsProperty);
  store_.RegisterBool(kLinkMonitorDisableProperty, &link_monitor_disabled_);
  store_.RegisterBool(kManagedCredentialsProperty, &managed_credentials_);
  HelpRegisterDerivedBool(kMeteredProperty, &Service::GetMeteredProperty,
                          &Service::SetMeteredProperty,
                          &Service::ClearMeteredProperty);

  HelpRegisterDerivedBool(kVisibleProperty, &Service::GetVisibleProperty,
                          nullptr, nullptr);

  store_.RegisterConstString(kProbeUrlProperty, &probe_url_string_);

  HelpRegisterDerivedString(kONCSourceProperty, &Service::GetONCSource,
                            &Service::SetONCSource);
  HelpRegisterConstDerivedUint64(kTrafficCounterResetTimeProperty,
                                 &Service::GetTrafficCounterResetTimeProperty);

  HelpRegisterConstDerivedUint64(kLastManualConnectAttemptProperty,
                                 &Service::GetLastManualConnectAttemptProperty);
  HelpRegisterConstDerivedUint64(kLastConnectedProperty,
                                 &Service::GetLastConnectedProperty);
  HelpRegisterConstDerivedUint64(kLastOnlineProperty,
                                 &Service::GetLastOnlineProperty);
  HelpRegisterConstDerivedUint64(kStartTimeProperty,
                                 &Service::GetStartTimeProperty);
  HelpRegisterConstDerivedInt32(kNetworkIDProperty, &Service::GetNetworkID);

  store_.RegisterConstUint32(kUplinkSpeedPropertyKbps, &uplink_speed_kbps_);
  store_.RegisterConstUint32(kDownlinkSpeedPropertyKbps, &downlink_speed_kbps_);

  service_metrics_ = std::make_unique<ServiceMetrics>();
  InitializeServiceStateTransitionMetrics();

  static_ip_parameters_.PlumbPropertyStore(&store_);
  store_.RegisterDerivedKeyValueStore(
      kSavedIPConfigProperty,
      KeyValueStoreAccessor(new CustomAccessor<Service, KeyValueStore>(
          this, &Service::GetSavedIPConfig, nullptr)));

  store_.RegisterDerivedKeyValueStore(
      kNetworkConfigProperty,
      KeyValueStoreAccessor(new CustomAccessor<Service, KeyValueStore>(
          this, &Service::GetNetworkConfigDict, nullptr)));

  IgnoreParameterForConfigure(kTypeProperty);
  IgnoreParameterForConfigure(kProfileProperty);

  SetStartTimeProperty(base::Time::Now());

  SLOG(1) << *this << ": Service constructed";
}

Service::~Service() {
  if (attached_network_) {
    LOG(WARNING) << *this << ": Service still had a Network attached";
    attached_network_->UnregisterEventHandler(network_event_handler_.get());
  }
  SLOG(1) << *this << ": Service destroyed.";
}

void Service::AutoConnect() {
  if (!auto_connect()) {
    return;
  }

  const char* reason = nullptr;
  if (!IsAutoConnectable(&reason)) {
    if (reason == kAutoConnTechnologyNotAutoConnectable ||
        reason == kAutoConnConnected) {
      SLOG(2) << *this << " " << __func__
              << ": Suppressed autoconnect:" << reason;
    } else if (reason == kAutoConnBusy ||
               reason == kAutoConnMediumUnavailable) {
      SLOG(1) << *this << " " << __func__ << ": Suppressed autoconnect"
              << reason;
    } else {
      SLOG(2) << *this << " " << __func__
              << ": Suppressed autoconnect: " << reason;
    }
    return;
  }

  Error error;
  LOG(INFO) << *this << " " << __func__ << ": Auto-connecting";
  ThrottleFutureAutoConnects();
  is_in_auto_connect_ = true;
  Connect(&error, __func__);
  // If Service::Connect returns with error, roll-back the flag that marks
  // auto-connection is ongoing so that next sessions are not affected.
  if (error.IsFailure() || IsInFailState()) {
    is_in_auto_connect_ = false;
  }
}

void Service::Connect(Error* error, const char* reason) {
  CHECK(reason);
  // If there is no record of a manual connect, record the first time a
  // connection is attempted so there is way to track how long it's been since
  // the first connection attempt.
  if (last_manual_connect_attempt_.ToDeltaSinceWindowsEpoch().is_zero()) {
    SetLastManualConnectAttemptProperty(base::Time::Now());
  }

  if (!connectable()) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kOperationFailed,
        base::StringPrintf(
            "Connect attempted but %s Service %s is not connectable: %s",
            GetTechnologyName().c_str(), log_name().c_str(), reason));
    return;
  }

  if (IsConnected()) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kAlreadyConnected,
        base::StringPrintf(
            "Connect attempted but %s Service %s is already connected: %s",
            GetTechnologyName().c_str(), log_name().c_str(), reason));
    return;
  }
  if (IsConnecting()) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kInProgress,
        base::StringPrintf(
            "Connect attempted but %s Service %s already connecting: %s",
            GetTechnologyName().c_str(), log_name().c_str(), reason));
    return;
  }
  if (IsDisconnecting()) {
    // SetState will re-trigger a connection after this disconnection has
    // completed.
    pending_connect_task_.Reset(
        base::BindOnce(&Service::Connect, weak_ptr_factory_.GetWeakPtr(),
                       base::Owned(new Error()), "Triggering delayed Connect"));
    return;
  }

  pending_connect_task_.Cancel();
  // This cannot be called until here because |explicitly_disconnected_| is
  // used in determining whether or not this Service can be AutoConnected.
  ClearExplicitlyDisconnected();

  // Note: this log is parsed by logprocessor based on |reason|.
  LOG(INFO) << *this << " " << __func__ << ": " << reason;

  // Clear any failure state from a previous connect attempt.
  if (IsInFailState()) {
    SetState(kStateIdle);
  }

  // Perform connection logic defined by children. This logic will
  // drive the state from kStateIdle.
  OnConnect(error);
}

void Service::Disconnect(Error* error, const char* reason) {
  CHECK(reason);
  if (!IsDisconnectable(error)) {
    LOG(WARNING) << *this << " " << __func__
                 << ": not disconnectable: " << reason;
    return;
  }

  LOG(INFO) << *this << " " << __func__ << ": " << reason;
  SetState(kStateDisconnecting);
  // Perform connection logic defined by children. This logic will
  // drive the state to kStateIdle.
  OnDisconnect(error, reason);
}

void Service::DisconnectWithFailure(ConnectFailure failure,
                                    Error* error,
                                    const char* reason) {
  SLOG(1) << *this << " " << __func__ << ": "
          << ConnectFailureToString(failure);
  CHECK(reason);
  Disconnect(error, reason);
  SetFailure(failure);
}

void Service::UserInitiatedConnect(const char* reason, Error* error) {
  SLOG(3) << *this << " " << __func__;
  SetLastManualConnectAttemptProperty(base::Time::Now());
  // |is_in_user_connect_| should only be set when Service::Connect returns with
  // no error, i.e. the connection attempt is successfully initiated. However,
  // when the call stack of Service::Connect gets far enough and no error is
  // expected, it is useful to distinguish whether the connection is initiated
  // by the user. Here, optimistically set this field in advance (assume the
  // initiation of a connection attempt will succeed) and roll-back when
  // Service::Connect returns with error.
  is_in_user_connect_ = true;
  Connect(error, reason);

  // Since Service::Connect will clear a failure state when it gets far enough,
  // we know that |error| not indicating an failure but this instance being in a
  // failure state means that a Device drove the state to failure. We do this
  // because Ethernet and WiFi currently don't have |error| passed down to
  // ConnectTo.
  //
  // TODO(crbug.com/206812) Pipe |error| through to WiFi and Ethernet ConnectTo.
  if (error->IsFailure() || IsInFailState()) {
    if (connectable() && error->type() != Error::kAlreadyConnected &&
        error->type() != Error::kInProgress) {
      ReportUserInitiatedConnectionResult(state());
    }
    // The initiation of the connection attempt failed, we're not even going to
    // ask lower layers (e.g. wpa_supplicant for WiFi) to connect, so the flag
    // won't be cleared in Service::SetState when the connection attempt would
    // succeed/fail. Reset the flag so it doesn't interfere with the next
    // connection attempt.
    is_in_user_connect_ = false;
  }
}

void Service::UserInitiatedDisconnect(const char* reason, Error* error) {
  // |explicitly_disconnected_| should be set prior to calling Disconnect, as
  // Disconnect flows could otherwise potentially hit NoteFailureEvent prior to
  // this being set.
  explicitly_disconnected_ = true;
  Disconnect(error, Service::kDisconnectReasonDbus);
}

void Service::CompleteCellularActivation(Error* error) {
  Error::PopulateAndLog(FROM_HERE, error, Error::kNotImplemented,
                        "Service doesn't support cellular activation "
                        "completion for technology: " +
                            GetTechnologyName());
}

std::string Service::GetWiFiPassphrase(Error* error) {
  Error::PopulateAndLog(FROM_HERE, error, Error::kNotImplemented,
                        "Service doesn't support WiFi passphrase retrieval for "
                        "technology: " +
                            GetTechnologyName());
  return std::string();
}

bool Service::IsActive(Error* /*error*/) const {
  return state() != kStateUnknown && state() != kStateIdle &&
         state() != kStateFailure && state() != kStateDisconnecting;
}

// static
bool Service::IsConnectedState(ConnectState state) {
  return (state == kStateConnected || IsPortalledState(state) ||
          state == kStateOnline);
}

// static
bool Service::IsConnectingState(ConnectState state) {
  return (state == kStateAssociating || state == kStateConfiguring);
}

// static
bool Service::IsPortalledState(ConnectState state) {
  return state == kStateNoConnectivity || state == kStateRedirectFound;
}

bool Service::IsConnected(Error* /*error*/) const {
  return IsConnectedState(state());
}

bool Service::IsConnecting() const {
  return IsConnectingState(state());
}

bool Service::IsDisconnecting() const {
  return state() == kStateDisconnecting;
}

bool Service::IsPortalled() const {
  return IsPortalledState(state());
}

bool Service::IsFailed() const {
  // We sometimes lie about the failure state, to keep Chrome happy
  // (see comment in WiFi::HandleDisconnect). Hence, we check both
  // state and |failed_time_|.
  return state() == kStateFailure || !failed_time_.is_null();
}

bool Service::IsInFailState() const {
  return state() == kStateFailure;
}

bool Service::IsOnline() const {
  return state() == kStateOnline;
}

void Service::ResetAutoConnectCooldownTime() {
  auto_connect_cooldown_ = base::TimeDelta();
  reenable_auto_connect_task_.Cancel();
}

void Service::SetState(ConnectState state) {
  if (state == state_) {
    return;
  }

  // Note: this log is parsed by logprocessor.
  LOG(INFO) << *this << " " << __func__ << ": state "
            << ConnectStateToString(state_) << " -> "
            << ConnectStateToString(state);

  if (!pending_connect_task_.IsCancelled() &&
      (state == kStateFailure || state == kStateIdle)) {
    dispatcher()->PostTask(FROM_HERE, pending_connect_task_.callback());
  }

  // Metric reporting for result of user-initiated connection attempt.
  if ((is_in_user_connect_ || is_in_auto_connect_) &&
      ((state == kStateConnected) || (state == kStateFailure) ||
       (state == kStateIdle))) {
    if (is_in_user_connect_) {
      ReportUserInitiatedConnectionResult(state);
      is_in_user_connect_ = false;
    }
    if (is_in_auto_connect_) {
      is_in_auto_connect_ = false;
    }
  }

  if (state == kStateFailure) {
    NoteFailureEvent();
  }

  previous_state_ = state_;
  state_ = state;
  if (state != kStateFailure) {
    failure_ = kFailureNone;
    SetErrorDetails(kErrorDetailsNone);
  }
  if (state == kStateConnected) {
    failed_time_ = base::Time();
    has_ever_connected_ = true;
    SetLastConnectedProperty(base::Time::Now());
    SaveToProfile();
    // When we succeed in connecting, forget that connects failed in the past.
    // Give services one chance at a fast autoconnect retry by resetting the
    // cooldown to 0 to indicate that the last connect was successful.
    ResetAutoConnectCooldownTime();
  }
  // Because we can bounce between `online` and 'limited-connectivity' states
  // while connected, this value will store the last time the service
  // transitioned to the `online` state.
  if (state == kStateOnline) {
    SetLastOnlineProperty(base::Time::Now());
  }

  UpdateErrorProperty();
  manager_->NotifyServiceStateChanged(this);
  UpdateStateTransitionMetrics(state);

  if (IsConnectedState(previous_state_) != IsConnectedState(state_)) {
    adaptor_->EmitBoolChanged(kIsConnectedProperty, IsConnected());
  }
  adaptor_->EmitStringChanged(kStateProperty, GetStateString());
}

void Service::SetProbeUrl(const std::string& probe_url_string) {
  if (probe_url_string_ == probe_url_string) {
    return;
  }
  probe_url_string_ = probe_url_string;
  adaptor_->EmitStringChanged(kProbeUrlProperty, probe_url_string);
}

void Service::ReEnableAutoConnectTask() {
  // Kill the thing blocking AutoConnect().
  reenable_auto_connect_task_.Cancel();
  // Post to the manager, giving it an opportunity to AutoConnect again.
  manager_->UpdateService(this);
}

void Service::ThrottleFutureAutoConnects() {
  if (!auto_connect_cooldown_.is_zero()) {
    LOG(INFO) << *this << " " << __func__ << ": Next autoconnect in "
              << auto_connect_cooldown_;
    reenable_auto_connect_task_.Reset(base::BindOnce(
        &Service::ReEnableAutoConnectTask, weak_ptr_factory_.GetWeakPtr()));
    dispatcher()->PostDelayedTask(FROM_HERE,
                                  reenable_auto_connect_task_.callback(),
                                  auto_connect_cooldown_);
  }
  auto min_cooldown_time =
      std::max(GetMinAutoConnectCooldownTime(),
               auto_connect_cooldown_ * kAutoConnectCooldownBackoffFactor);
  auto_connect_cooldown_ =
      std::min(GetMaxAutoConnectCooldownTime(), min_cooldown_time);
}

void Service::SaveFailure() {
  previous_error_ = ConnectFailureToString(failure_);
  ++previous_error_serial_number_;
}

void Service::SetFailure(ConnectFailure failure) {
  SLOG(1) << *this << " " << __func__ << ": "
          << ConnectFailureToString(failure);
  failure_ = failure;
  failed_time_ = base::Time::Now();
  SaveFailure();
  UpdateErrorProperty();
  SetState(kStateFailure);
}

void Service::SetFailureSilent(ConnectFailure failure) {
  SLOG(1) << *this << " " << __func__ << ": "
          << ConnectFailureToString(failure);
  NoteFailureEvent();
  // Note that order matters here, since SetState modifies |failure_| and
  // |failed_time_|.
  SetState(kStateIdle);
  failure_ = failure;
  failed_time_ = base::Time::Now();
  SaveFailure();
  UpdateErrorProperty();
}

std::optional<base::TimeDelta> Service::GetTimeSinceFailed() const {
  if (failed_time_.is_null()) {
    return std::nullopt;
  }
  return base::Time::Now() - failed_time_;
}

std::string Service::GetDBusObjectPathIdentifier() const {
  return base::NumberToString(serial_number());
}

const RpcIdentifier& Service::GetRpcIdentifier() const {
  return adaptor_->GetRpcIdentifier();
}

std::string Service::GetLoadableStorageIdentifier(
    const StoreInterface& storage) const {
  return IsLoadableFrom(storage) ? GetStorageIdentifier() : "";
}

bool Service::IsLoadableFrom(const StoreInterface& storage) const {
  return storage.ContainsGroup(GetStorageIdentifier());
}

Service::ONCSource Service::ParseONCSourceFromUIData() {
  // If ONC Source was not stored directly, we may still guess it
  // from ONC Data blob.
  if (ui_data_.find("\"onc_source\":\"device_policy\"") != std::string::npos) {
    return ONCSource::kONCSourceDevicePolicy;
  }
  if (ui_data_.find("\"onc_source\":\"user_policy\"") != std::string::npos) {
    return ONCSource::kONCSourceUserPolicy;
  }
  if (ui_data_.find("\"onc_source\":\"user_import\"") != std::string::npos) {
    return ONCSource::kONCSourceUserImport;
  }
  return ONCSource::kONCSourceUnknown;
}

bool Service::Load(const StoreInterface* storage) {
  const auto id = GetStorageIdentifier();
  if (!storage->ContainsGroup(id)) {
    LOG(WARNING) << *this << " " << __func__
                 << ": Service is not available in the persistent store: "
                 << id;
    return false;
  }

  auto_connect_ = IsAutoConnectByDefault();
  retain_auto_connect_ =
      storage->GetBool(id, kStorageAutoConnect, &auto_connect_);

  std::string check_portal_name;
  storage->GetString(id, kStorageCheckPortal, &check_portal_name);
  check_portal_ = CheckPortalStateFromString(check_portal_name)
                      .value_or(CheckPortalState::kAutomatic);
  SetCACertExperimentPhase(manager_->GetCACertExperimentPhase());

  LoadString(storage, id, kStorageGUID, "", &guid_);
  if (!storage->GetInt(id, kStoragePriority, &priority_)) {
    priority_ = kPriorityNone;
  }
  LoadString(storage, id, kStorageProxyConfig, "", &proxy_config_);
  storage->GetBool(id, kStorageSaveCredentials, &save_credentials_);
  LoadString(storage, id, kStorageUIData, "", &ui_data_);

  // Check if service comes from a managed policy.
  int source;
  auto ret = storage->GetInt(id, kStorageONCSource, &source);
  if (!ret || (source > static_cast<int>(ONCSource::kONCSourceUserPolicy))) {
    source_ = ONCSource::kONCSourceUnknown;
  } else {
    source_ = static_cast<ONCSource>(source);
  }
  SLOG(2) << *this << " " << __func__
          << ": Service source = " << static_cast<size_t>(source_);

  if (!storage->GetBool(id, kStorageManagedCredentials,
                        &managed_credentials_)) {
    managed_credentials_ = false;
  }

  bool metered_override;
  if (storage->GetBool(id, kStorageMeteredOverride, &metered_override)) {
    metered_override_ = metered_override;
  }

  // Note that service might be connected when Load() is called, e.g., Ethernet
  // service will keep connected when profile is changed.
  if (static_ip_parameters_.Load(storage, id)) {
    NotifyStaticIPConfigChanged();
  }

  // Call OnEapCredentialsChanged with kReasonCredentialsLoaded to avoid
  // resetting the has_ever_connected value.
  if (mutable_eap()) {
    mutable_eap()->Load(storage, id);
    OnEapCredentialsChanged(kReasonCredentialsLoaded);
  }

  ClearExplicitlyDisconnected();

  // Read has_ever_connected_ value from stored profile
  // now that the credentials have been loaded.
  storage->GetBool(id, kStorageHasEverConnected, &has_ever_connected_);

  storage->GetBool(id, kStorageEnableRFC8925, &enable_rfc_8925_);

  for (auto source : patchpanel::Client::kAllTrafficSources) {
    patchpanel::Client::TrafficVector counters = {};
    storage->GetUint64(id,
                       GetCurrentTrafficCounterKey(
                           source, kStorageTrafficCounterRxBytesSuffix),
                       &counters.rx_bytes);
    storage->GetUint64(id,
                       GetCurrentTrafficCounterKey(
                           source, kStorageTrafficCounterTxBytesSuffix),
                       &counters.tx_bytes);
    storage->GetUint64(id,
                       GetCurrentTrafficCounterKey(
                           source, kStorageTrafficCounterRxPacketsSuffix),
                       &counters.rx_packets);
    storage->GetUint64(id,
                       GetCurrentTrafficCounterKey(
                           source, kStorageTrafficCounterTxPacketsSuffix),
                       &counters.tx_packets);
    if (counters.rx_bytes == 0 && counters.tx_bytes == 0) {
      continue;
    }
    current_total_traffic_counters_[source] = counters;
    total_traffic_counter_snapshot_[source] = counters;
  }

  uint64_t temp_ms;
  if (storage->GetUint64(id, kStorageTrafficCounterResetTime, &temp_ms)) {
    traffic_counter_reset_time_ =
        base::Time::FromDeltaSinceWindowsEpoch(base::Milliseconds(temp_ms));
  }
  if (storage->GetUint64(id, kStorageLastManualConnectAttempt, &temp_ms)) {
    last_manual_connect_attempt_ =
        base::Time::FromDeltaSinceWindowsEpoch(base::Milliseconds(temp_ms));
  }
  if (storage->GetUint64(id, kStorageLastConnected, &temp_ms)) {
    last_connected_ =
        base::Time::FromDeltaSinceWindowsEpoch(base::Milliseconds(temp_ms));
  }
  if (storage->GetUint64(id, kStorageLastOnline, &temp_ms)) {
    last_online_ =
        base::Time::FromDeltaSinceWindowsEpoch(base::Milliseconds(temp_ms));
  }
  if (storage->GetUint64(id, kStorageStartTime, &temp_ms)) {
    start_time_ =
        base::Time::FromDeltaSinceWindowsEpoch(base::Milliseconds(temp_ms));
  }
  return true;
}

void Service::MigrateDeprecatedStorage(StoreInterface* storage) {
  const auto id = GetStorageIdentifier();
  CHECK(storage->ContainsGroup(id));

  // TODO(b/357355410): Remove this in the next stepping milestone after M131.
  storage->DeleteKey(id, kStorageDeprecatedLinkMonitorDisabled);

  // TODO(b/309607419): Remove code deleting traffic counter storage keys made
  // obsolete by crrev/c/5014643 and crrev/c/4535677.
  constexpr static std::array<std::string_view, 2>
      kObsoleteTrafficCounterSourceNames({"CROSVM", "PLUGINVM"});
  for (auto source : kObsoleteTrafficCounterSourceNames) {
    storage->DeleteKey(
        id, base::StrCat({source, kStorageTrafficCounterRxBytesSuffix}));
    storage->DeleteKey(
        id, base::StrCat({source, kStorageTrafficCounterTxBytesSuffix}));
    storage->DeleteKey(
        id, base::StrCat({source, kStorageTrafficCounterRxPacketsSuffix}));
    storage->DeleteKey(
        id, base::StrCat({source, kStorageTrafficCounterTxPacketsSuffix}));
  }
}

bool Service::Unload() {
  auto_connect_ = IsAutoConnectByDefault();
  retain_auto_connect_ = false;
  check_portal_ = CheckPortalState::kAutomatic;
  ClearExplicitlyDisconnected();
  guid_ = "";
  has_ever_connected_ = false;
  priority_ = kPriorityNone;
  proxy_config_ = "";
  save_credentials_ = true;
  ui_data_ = "";
  link_monitor_disabled_ = false;
  managed_credentials_ = false;
  source_ = ONCSource::kONCSourceUnknown;
  if (mutable_eap()) {
    mutable_eap()->Reset();
  }
  ClearEAPCertification();
  if (IsActive(nullptr)) {
    Error error;  // Ignored.
    Disconnect(&error, Service::kDisconnectReasonUnload);
  }
  current_total_traffic_counters_.clear();
  static_ip_parameters_.Reset();
  return false;
}

void Service::Remove(Error* /*error*/) {
  manager()->RemoveService(this);
  // |this| may no longer be valid now.
}

bool Service::Save(StoreInterface* storage) {
  const auto id = GetStorageIdentifier();

  storage->SetString(id, kStorageType, GetTechnologyName());

  // IMPORTANT: Changes to kStorageAutoConnect must be backwards compatible, see
  // WiFiService::Save for details.
  if (retain_auto_connect_) {
    storage->SetBool(id, kStorageAutoConnect, auto_connect_);
  } else {
    storage->DeleteKey(id, kStorageAutoConnect);
  }

  storage->SetString(id, kStorageCheckPortal,
                     CheckPortalStateToString(check_portal_));
  SaveStringOrClear(storage, id, kStorageGUID, guid_);
  storage->SetBool(id, kStorageHasEverConnected, has_ever_connected_);
  storage->SetString(id, kStorageName, friendly_name_);
  if (priority_ != kPriorityNone) {
    storage->SetInt(id, kStoragePriority, priority_);
  } else {
    storage->DeleteKey(id, kStoragePriority);
  }
  SaveStringOrClear(storage, id, kStorageProxyConfig, proxy_config_);
  storage->SetBool(id, kStorageSaveCredentials, save_credentials_);
  SaveStringOrClear(storage, id, kStorageUIData, ui_data_);
  storage->SetInt(id, kStorageONCSource, static_cast<int>(source_));
  storage->SetBool(id, kStorageManagedCredentials, managed_credentials_);
  storage->SetBool(id, kEnableRFC8925Property, enable_rfc_8925_);

  if (metered_override_.has_value()) {
    storage->SetBool(id, kStorageMeteredOverride, metered_override_.value());
  } else {
    storage->DeleteKey(id, kStorageMeteredOverride);
  }

  static_ip_parameters_.Save(storage, id);
  if (eap()) {
    eap()->Save(storage, id, save_credentials_);
  }

  for (auto source : patchpanel::Client::kAllTrafficSources) {
    const auto& counter = current_total_traffic_counters_[source];
    if (counter == patchpanel::Client::kZeroTraffic) {
      continue;
    }
    storage->SetUint64(id,
                       GetCurrentTrafficCounterKey(
                           source, kStorageTrafficCounterRxBytesSuffix),
                       counter.rx_bytes);
    storage->SetUint64(id,
                       GetCurrentTrafficCounterKey(
                           source, kStorageTrafficCounterTxBytesSuffix),
                       counter.tx_bytes);
    storage->SetUint64(id,
                       GetCurrentTrafficCounterKey(
                           source, kStorageTrafficCounterRxPacketsSuffix),
                       counter.rx_packets);
    storage->SetUint64(id,
                       GetCurrentTrafficCounterKey(
                           source, kStorageTrafficCounterTxPacketsSuffix),
                       counter.tx_packets);
  }

  storage->SetUint64(id, kStorageTrafficCounterResetTime,
                     GetTrafficCounterResetTimeProperty(/*error=*/nullptr));

  if (!last_manual_connect_attempt_.ToDeltaSinceWindowsEpoch().is_zero()) {
    storage->SetUint64(id, kStorageLastManualConnectAttempt,
                       GetLastManualConnectAttemptProperty(/*error=*/nullptr));
  }

  if (!last_connected_.ToDeltaSinceWindowsEpoch().is_zero()) {
    storage->SetUint64(id, kStorageLastConnected,
                       GetLastConnectedProperty(/*error=*/nullptr));
  }

  if (!last_online_.ToDeltaSinceWindowsEpoch().is_zero()) {
    storage->SetUint64(id, kStorageLastOnline,
                       GetLastOnlineProperty(/*error=*/nullptr));
  }
  if (!start_time_.ToDeltaSinceWindowsEpoch().is_zero()) {
    storage->SetUint64(id, kStorageStartTime,
                       GetStartTimeProperty(/*error=*/nullptr));
  }

  return true;
}

void Service::Configure(const KeyValueStore& args, Error* error) {
  for (const auto& it : args.properties()) {
    if (it.second.IsTypeCompatible<bool>()) {
      if (base::Contains(parameters_ignored_for_configure_, it.first)) {
        SLOG(5) << *this << " " << __func__
                << ": Ignoring bool property: " << it.first;
        continue;
      }
      SLOG(5) << *this << " " << __func__
              << ": Configuring bool property: " << it.first;
      Error set_error;
      store_.SetBoolProperty(it.first, it.second.Get<bool>(), &set_error);
      if (error->IsSuccess() && set_error.IsFailure()) {
        *error = set_error;
      }
    } else if (it.second.IsTypeCompatible<int32_t>()) {
      if (base::Contains(parameters_ignored_for_configure_, it.first)) {
        SLOG(5) << *this << " " << __func__
                << ": Ignoring int32_t property: " << it.first;
        continue;
      }
      SLOG(5) << *this << " " << __func__
              << ": Configuring int32_t property: " << it.first;
      Error set_error;
      store_.SetInt32Property(it.first, it.second.Get<int32_t>(), &set_error);
      if (error->IsSuccess() && set_error.IsFailure()) {
        *error = set_error;
      }
    } else if (it.second.IsTypeCompatible<KeyValueStore>()) {
      if (base::Contains(parameters_ignored_for_configure_, it.first)) {
        SLOG(5) << *this << " " << __func__
                << ": Ignoring key value store property: " << it.first;
        continue;
      }
      SLOG(5) << *this << " " << __func__
              << ": Configuring key value store property: " << it.first;
      Error set_error;
      store_.SetKeyValueStoreProperty(it.first, it.second.Get<KeyValueStore>(),
                                      &set_error);
      if (error->IsSuccess() && set_error.IsFailure()) {
        *error = set_error;
      }
    } else if (it.second.IsTypeCompatible<std::string>()) {
      if (base::Contains(parameters_ignored_for_configure_, it.first)) {
        SLOG(5) << *this << " " << __func__
                << ": Ignoring string property: " << it.first;
        continue;
      }
      SLOG(6) << *this << " " << __func__
              << ": Configuring string property: " << it.first;
      Error set_error;
      store_.SetStringProperty(it.first, it.second.Get<std::string>(),
                               &set_error);
      if (error->IsSuccess() && set_error.IsFailure()) {
        *error = set_error;
      }
    } else if (it.second.IsTypeCompatible<Strings>()) {
      if (base::Contains(parameters_ignored_for_configure_, it.first)) {
        SLOG(5) << *this << " " << __func__
                << ": Ignoring strings property: " << it.first;
        continue;
      }
      SLOG(5) << *this << " " << __func__
              << ": Configuring strings property: " << it.first;
      Error set_error;
      store_.SetStringsProperty(it.first, it.second.Get<Strings>(), &set_error);
      if (error->IsSuccess() && set_error.IsFailure()) {
        *error = set_error;
      }
    } else if (it.second.IsTypeCompatible<Stringmap>()) {
      if (base::Contains(parameters_ignored_for_configure_, it.first)) {
        SLOG(5) << *this << " " << __func__
                << ": Ignoring stringmap property: " << it.first;
        continue;
      }
      SLOG(5) << *this << " " << __func__
              << ": Configuring stringmap property: " << it.first;
      Error set_error;
      store_.SetStringmapProperty(it.first, it.second.Get<Stringmap>(),
                                  &set_error);
      if (error->IsSuccess() && set_error.IsFailure()) {
        *error = set_error;
      }
    } else if (it.second.IsTypeCompatible<Stringmaps>()) {
      if (base::Contains(parameters_ignored_for_configure_, it.first)) {
        SLOG(5) << *this << " " << __func__
                << ": Ignoring stringmaps property: " << it.first;
        continue;
      }
      SLOG(5) << *this << " " << __func__
              << ": Configuring stringmaps property: " << it.first;
      Error set_error;
      store_.SetStringmapsProperty(it.first, it.second.Get<Stringmaps>(),
                                   &set_error);
      if (error->IsSuccess() && set_error.IsFailure()) {
        *error = set_error;
      }
    }
  }
}

bool Service::DoPropertiesMatch(const KeyValueStore& args) const {
  for (const auto& it : args.properties()) {
    if (it.second.IsTypeCompatible<bool>()) {
      SLOG(5) << *this << " " << __func__
              << ": Checking bool property: " << it.first;
      Error get_error;
      bool value;
      if (!store_.GetBoolProperty(it.first, &value, &get_error) ||
          value != it.second.Get<bool>()) {
        return false;
      }
    } else if (it.second.IsTypeCompatible<int32_t>()) {
      SLOG(5) << *this << " " << __func__
              << ": Checking int32 property: " << it.first;
      Error get_error;
      int32_t value;
      if (!store_.GetInt32Property(it.first, &value, &get_error) ||
          value != it.second.Get<int32_t>()) {
        return false;
      }
    } else if (it.second.IsTypeCompatible<std::string>()) {
      SLOG(5) << *this << " " << __func__
              << ": Checking string property: " << it.first;
      Error get_error;
      std::string value;
      if (!store_.GetStringProperty(it.first, &value, &get_error) ||
          value != it.second.Get<std::string>()) {
        return false;
      }
    } else if (it.second.IsTypeCompatible<Strings>()) {
      SLOG(5) << *this << " " << __func__
              << ": Checking strings property: " << it.first;
      Error get_error;
      Strings value;
      if (!store_.GetStringsProperty(it.first, &value, &get_error) ||
          value != it.second.Get<Strings>()) {
        return false;
      }
    } else if (it.second.IsTypeCompatible<Stringmap>()) {
      SLOG(5) << *this << " " << __func__
              << ": Checking stringmap property: " << it.first;
      Error get_error;
      Stringmap value;
      if (!store_.GetStringmapProperty(it.first, &value, &get_error) ||
          value != it.second.Get<Stringmap>()) {
        return false;
      }
    } else if (it.second.IsTypeCompatible<KeyValueStore>()) {
      SLOG(5) << *this << " " << __func__
              << ": Checking key value store property: " << it.first;
      Error get_error;
      KeyValueStore value;
      if (!store_.GetKeyValueStoreProperty(it.first, &value, &get_error) ||
          value != it.second.Get<KeyValueStore>()) {
        return false;
      }
    }
  }
  return true;
}

bool Service::IsRemembered() const {
  return profile_ && !manager_->IsServiceEphemeral(this);
}

bool Service::HasProxyConfig() const {
  if (proxy_config_.empty()) {
    return false;
  }

  // Check if proxy "mode" is equal to "direct".
  auto mode = GetJSONDictValue(proxy_config_, kServiceProxyConfigMode);
  if (!mode) {
    LOG(ERROR) << *this << " " << __func__
               << ": Failed to parse proxy config: " << proxy_config_;
    // Returns true here for backward compatibility. Previously, this method
    // only checks whether or not |proxy_config_| is empty.
    return true;
  }
  return *mode != kServiceProxyConfigModeDirect;
}

void Service::EnableAndRetainAutoConnect() {
  if (retain_auto_connect_) {
    // We do not want to clobber the value of auto_connect_ (it may
    // be user-set). So return early.
    return;
  }

  SetAutoConnect(true);
  RetainAutoConnect();
}

void Service::AttachNetwork(base::WeakPtr<Network> network) {
  if (attached_network_) {
    LOG(ERROR) << *this << " " << __func__ << ": Network was already attached.";
    DetachNetwork();
  }
  if (!network) {
    LOG(ERROR) << *this << " " << __func__ << ": Cannot attach null Network";
    return;
  }
  attached_network_ = network;
  attached_network_->SetServiceLoggingName(log_name());
  adaptor_->EmitIntChanged(kNetworkIDProperty, network->network_id());
  EmitIPConfigPropertyChange();
  EmitNetworkConfigPropertyChange();
  attached_network_->RegisterCurrentIPConfigChangeHandler(base::BindRepeating(
      &Service::EmitIPConfigPropertyChange, weak_ptr_factory_.GetWeakPtr()));
  attached_network_->OnStaticIPConfigChanged(static_ip_parameters_.config());
  attached_network_->RegisterEventHandler(network_event_handler_.get());
  RefreshTrafficCountersTask(/*initialize=*/true);
}

void Service::DetachNetwork() {
  if (!attached_network_) {
    LOG(ERROR) << *this << " " << __func__ << ": no Network to detach";
    return;
  }
  // Cancel traffic counter refresh recurring task and schedule immediately a
  // final traffic counter refresh.
  refresh_traffic_counter_task_.Cancel();
  RequestRawTrafficCounters(base::BindOnce(&Service::RefreshTrafficCounters,
                                           weak_ptr_factory_.GetWeakPtr()));
  // Clear the handler and static IP config registered on the previous
  // Network.
  attached_network_->UnregisterEventHandler(network_event_handler_.get());
  attached_network_->RegisterCurrentIPConfigChangeHandler({});
  attached_network_->OnStaticIPConfigChanged({});
  attached_network_->ClearServiceLoggingName();
  attached_network_ = nullptr;
  EmitNetworkConfigPropertyChange();
  EmitIPConfigPropertyChange();
  adaptor_->EmitIntChanged(kNetworkIDProperty, 0);
}

void Service::EmitIPConfigPropertyChange() {
  Error error;
  RpcIdentifier ipconfig = GetIPConfigRpcIdentifier(&error);
  if (error.IsSuccess()) {
    adaptor_->EmitRpcIdentifierChanged(kIPConfigProperty, ipconfig);
  }
}

void Service::NotifyStaticIPConfigChanged() {
  if (attached_network_) {
    attached_network_->OnStaticIPConfigChanged(static_ip_parameters_.config());
  }
}

KeyValueStore Service::GetSavedIPConfig(Error* /*error*/) {
  if (!attached_network_) {
    return {};
  }
  const auto* saved_network_config = attached_network_->GetSavedIPConfig();
  return StaticIPParameters::NetworkConfigToKeyValues(
      saved_network_config ? *saved_network_config : net_base::NetworkConfig{});
}

// Helper functions used in `GetNetworkConfigDict()` below. Ideally these can be
// lambda functions defined only in that function, but cpplint is unhappy with
// the templated lambda functions and NOLINT does not help (b/303257041).
namespace {

// Updates the value for |key| in dict |kvs|. If |val| is nullopt, sets the
// value to an empty string; otherwise set the value to `val->ToString()`.
template <class T>
void KeyValueStoreSetStringFromOptional(std::string_view key,
                                        const std::optional<T>& val,
                                        KeyValueStore& kvs) {
  kvs.Set<std::string>(key, val.has_value() ? val->ToString() : "");
}

// Updates the value for |key| in dict |kvs|. Call `ToString()` on each item in
// |vec| and sets the value to the resulted vector.
template <class T>
void KeyValueStoreSetStringsFromVector(std::string_view key,
                                       const std::vector<T>& vec,
                                       KeyValueStore& kvs) {
  Strings val;
  for (const auto& item : vec) {
    val.push_back(item.ToString());
  }
  kvs.Set<Strings>(key, val);
}

}  // namespace

KeyValueStore Service::GetNetworkConfigDict(Error* /*error*/) {
  if (!attached_network_) {
    return {};
  }
  const net_base::NetworkConfig& config = attached_network_->GetNetworkConfig();

  KeyValueStore kvs;

  // Use 0 as default value here to match the default value of network_id.
  kvs.Set<int>(kNetworkConfigSessionIDProperty,
               attached_network_->session_id().value_or(0));

  KeyValueStoreSetStringFromOptional(kNetworkConfigIPv4AddressProperty,
                                     config.ipv4_address, kvs);
  KeyValueStoreSetStringFromOptional(kNetworkConfigIPv4GatewayProperty,
                                     config.ipv4_gateway, kvs);
  KeyValueStoreSetStringsFromVector(kNetworkConfigIPv6AddressesProperty,
                                    config.ipv6_addresses, kvs);
  KeyValueStoreSetStringFromOptional(kNetworkConfigIPv6GatewayProperty,
                                     config.ipv6_gateway, kvs);
  KeyValueStoreSetStringsFromVector(kNetworkConfigNameServersProperty,
                                    config.dns_servers, kvs);
  kvs.Set<Strings>(kNetworkConfigSearchDomainsProperty,
                   config.dns_search_domains);
  KeyValueStoreSetStringsFromVector(kNetworkConfigIncludedRoutesProperty,
                                    config.included_route_prefixes, kvs);
  KeyValueStoreSetStringsFromVector(kNetworkConfigExcludedRoutesProperty,
                                    config.excluded_route_prefixes, kvs);
  KeyValueStoreSetStringFromOptional(kNetworkConfigPref64Property,
                                     config.pref64, kvs);
  kvs.Set<int>(kNetworkConfigMTUProperty, config.mtu.value_or(0));

  return kvs;
}

VirtualDeviceRefPtr Service::GetVirtualDevice() const {
  return nullptr;
}

bool Service::Is8021xConnectable() const {
  return eap() && eap()->IsConnectable();
}

bool Service::AddEAPCertification(const std::string& name, size_t depth) {
  if (depth >= kEAPMaxCertificationElements) {
    LOG(WARNING) << *this << " " << __func__ << ": Ignoring certification "
                 << name << " because depth " << depth
                 << " exceeds our maximum of " << kEAPMaxCertificationElements;
    return false;
  }

  if (depth >= remote_certification_.size()) {
    remote_certification_.resize(depth + 1);
  } else if (name == remote_certification_[depth]) {
    return true;
  }

  remote_certification_[depth] = name;
  LOG(INFO) << *this << " " << __func__ << ": Received certification for "
            << name << " at depth " << depth;
  return true;
}

void Service::ClearEAPCertification() {
  remote_certification_.clear();
}

void Service::SetEapSlotGetter(Pkcs11SlotGetter* slot_getter) {
  if (mutable_eap()) {
    mutable_eap()->SetEapSlotGetter(slot_getter);
  }
}

void Service::SetEapCredentials(EapCredentials* eap) {
  // This operation must be done at most once for the lifetime of the service.
  CHECK(eap && !eap_);

  eap_.reset(eap);
  eap_->InitPropertyStore(mutable_store());
}

std::string Service::GetEapPassphrase(Error* error) {
  if (eap()) {
    return eap()->GetEapPassword(error);
  }
  Error::PopulateAndLog(FROM_HERE, error, Error::kIllegalOperation,
                        "Cannot retrieve EAP passphrase from non-EAP network.");
  return std::string();
}

void Service::RequestPortalDetection(Error* error) {
  if (!IsConnected() || !attached_network_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kOperationFailed,
                          log_name() + " was not connected.");
    return;
  }
  LOG(INFO) << *this << " " << __func__;
  attached_network_->RequestNetworkValidation(
      NetworkMonitor::ValidationReason::kDBusRequest);
}

void Service::SetAutoConnect(bool connect) {
  if (auto_connect() == connect) {
    return;
  }
  LOG(INFO) << *this << " " << __func__ << ": " << connect;
  auto_connect_ = connect;
  adaptor_->EmitBoolChanged(kAutoConnectProperty, auto_connect());
}

// static
// Note: keep in sync with ERROR_* constants in
// android/system/connectivity/shill/IService.aidl.
const char* Service::ConnectFailureToString(ConnectFailure failure) {
  switch (failure) {
    case kFailureNone:
      return kErrorNoFailure;
    case kFailureAAA:
      return kErrorAaaFailed;
    case kFailureActivation:
      return kErrorActivationFailed;
    case kFailureBadPassphrase:
      return kErrorBadPassphrase;
    case kFailureBadWEPKey:
      return kErrorBadWEPKey;
    case kFailureConnect:
      return kErrorConnectFailed;
    case kFailureDNSLookup:
      return kErrorDNSLookupFailed;
    case kFailureDHCP:
      return kErrorDhcpFailed;
    case kFailureEAPAuthentication:
      return kErrorEapAuthenticationFailed;
    case kFailureEAPLocalTLS:
      return kErrorEapLocalTlsFailed;
    case kFailureEAPRemoteTLS:
      return kErrorEapRemoteTlsFailed;
    case kFailureHTTPGet:
      return kErrorHTTPGetFailed;
    case kFailureInternal:
      return kErrorInternal;
    case kFailureInvalidAPN:
      return kErrorInvalidAPN;
    case kFailureIPsecCertAuth:
      return kErrorIpsecCertAuthFailed;
    case kFailureIPsecPSKAuth:
      return kErrorIpsecPskAuthFailed;
    case kFailureNeedEVDO:
      return kErrorNeedEvdo;
    case kFailureNeedHomeNetwork:
      return kErrorNeedHomeNetwork;
    case kFailureOTASP:
      return kErrorOtaspFailed;
    case kFailureOutOfRange:
      return kErrorOutOfRange;
    case kFailurePinMissing:
      return kErrorPinMissing;
    case kFailurePPPAuth:
      return kErrorPppAuthFailed;
    case kFailureSimLocked:
      return kErrorSimLocked;
    case kFailureSimCarrierLocked:
      return kErrorSimCarrierLocked;
    case kFailureNotRegistered:
      return kErrorNotRegistered;
    case kFailureUnknown:
      return kErrorUnknownFailure;
    case kFailureNotAssociated:
      return kErrorNotAssociated;
    case kFailureNotAuthenticated:
      return kErrorNotAuthenticated;
    case kFailureTooManySTAs:
      return kErrorTooManySTAs;
    case kFailureDisconnect:
      return kErrorDisconnect;
    case kFailureDelayedConnectSetup:
      return kErrorDelayedConnectSetup;
    case kFailureSuspectInactiveSim:
      return kErrorSuspectInactiveSim;
    case kFailureSuspectSubscriptionError:
      return kErrorSuspectSubscriptionError;
    case kFailureSuspectModemDisallowed:
      return kErrorSuspectModemDisallowed;

    case kFailureMax:
      NOTREACHED_IN_MIGRATION();
  }
  return "Invalid";
}

// static
const char* Service::ConnectStateToString(ConnectState state) {
  switch (state) {
    case kStateUnknown:
      return "Unknown";
    case kStateIdle:
      return "Idle";
    case kStateAssociating:
      return "Associating";
    case kStateConfiguring:
      return "Configuring";
    case kStateConnected:
      return "Connected";
    case kStateNoConnectivity:
      return "No connectivity";
    case kStateRedirectFound:
      return "Redirect found";
    case kStateFailure:
      return "Failure";
    case kStateOnline:
      return "Online";
    case kStateDisconnecting:
      return "Disconnecting";
  }
  return "Invalid";
}

// static
Metrics::NetworkServiceError Service::ConnectFailureToMetricsEnum(
    Service::ConnectFailure failure) {
  // Explicitly map all possible failures. So when new failures are added,
  // they will need to be mapped as well. Otherwise, the compiler will
  // complain.
  switch (failure) {
    case Service::kFailureNone:
      return Metrics::kNetworkServiceErrorNone;
    case Service::kFailureAAA:
      return Metrics::kNetworkServiceErrorAAA;
    case Service::kFailureActivation:
      return Metrics::kNetworkServiceErrorActivation;
    case Service::kFailureBadPassphrase:
      return Metrics::kNetworkServiceErrorBadPassphrase;
    case Service::kFailureBadWEPKey:
      return Metrics::kNetworkServiceErrorBadWEPKey;
    case Service::kFailureConnect:
      return Metrics::kNetworkServiceErrorConnect;
    case Service::kFailureDHCP:
      return Metrics::kNetworkServiceErrorDHCP;
    case Service::kFailureDNSLookup:
      return Metrics::kNetworkServiceErrorDNSLookup;
    case Service::kFailureEAPAuthentication:
      return Metrics::kNetworkServiceErrorEAPAuthentication;
    case Service::kFailureEAPLocalTLS:
      return Metrics::kNetworkServiceErrorEAPLocalTLS;
    case Service::kFailureEAPRemoteTLS:
      return Metrics::kNetworkServiceErrorEAPRemoteTLS;
    case Service::kFailureHTTPGet:
      return Metrics::kNetworkServiceErrorHTTPGet;
    case Service::kFailureIPsecCertAuth:
      return Metrics::kNetworkServiceErrorIPsecCertAuth;
    case Service::kFailureIPsecPSKAuth:
      return Metrics::kNetworkServiceErrorIPsecPSKAuth;
    case Service::kFailureInternal:
      return Metrics::kNetworkServiceErrorInternal;
    case Service::kFailureInvalidAPN:
      return Metrics::kNetworkServiceErrorInvalidAPN;
    case Service::kFailureNeedEVDO:
      return Metrics::kNetworkServiceErrorNeedEVDO;
    case Service::kFailureNeedHomeNetwork:
      return Metrics::kNetworkServiceErrorNeedHomeNetwork;
    case Service::kFailureNotAssociated:
      return Metrics::kNetworkServiceErrorNotAssociated;
    case Service::kFailureNotAuthenticated:
      return Metrics::kNetworkServiceErrorNotAuthenticated;
    case Service::kFailureOTASP:
      return Metrics::kNetworkServiceErrorOTASP;
    case Service::kFailureOutOfRange:
      return Metrics::kNetworkServiceErrorOutOfRange;
    case Service::kFailurePPPAuth:
      return Metrics::kNetworkServiceErrorPPPAuth;
    case Service::kFailureSimLocked:
      return Metrics::kNetworkServiceErrorSimLocked;
    case Service::kFailureSimCarrierLocked:
      return Metrics::kNetworkServiceErrorSimCarrierLocked;
    case Service::kFailureNotRegistered:
      return Metrics::kNetworkServiceErrorNotRegistered;
    case Service::kFailurePinMissing:
      return Metrics::kNetworkServiceErrorPinMissing;
    case Service::kFailureTooManySTAs:
      return Metrics::kNetworkServiceErrorTooManySTAs;
    case Service::kFailureDisconnect:
      return Metrics::kNetworkServiceErrorDisconnect;
    case Service::kFailureDelayedConnectSetup:
      return Metrics::kNetworkServiceErrorDelayedConnectSetup;
    case Service::kFailureSuspectInactiveSim:
      return Metrics::kNetworkServiceErrorSuspectInactiveSim;
    case Service::kFailureSuspectSubscriptionError:
      return Metrics::kNetworkServiceErrorSuspectSubscriptionError;
    case Service::kFailureSuspectModemDisallowed:
      return Metrics::kNetworkServiceErrorSuspectModemDisallowed;
    case Service::kFailureUnknown:
    case Service::kFailureMax:
      return Metrics::kNetworkServiceErrorUnknown;
  }
}

// static
Metrics::UserInitiatedConnectionFailureReason
Service::ConnectFailureToFailureReason(Service::ConnectFailure failure) {
  switch (failure) {
    case Service::kFailureNone:
      return Metrics::kUserInitiatedConnectionFailureReasonNone;
    case Service::kFailureBadPassphrase:
      return Metrics::kUserInitiatedConnectionFailureReasonBadPassphrase;
    case Service::kFailureBadWEPKey:
      return Metrics::kUserInitiatedConnectionFailureReasonBadWEPKey;
    case Service::kFailureConnect:
      return Metrics::kUserInitiatedConnectionFailureReasonConnect;
    case Service::kFailureDHCP:
      return Metrics::kUserInitiatedConnectionFailureReasonDHCP;
    case Service::kFailureDNSLookup:
      return Metrics::kUserInitiatedConnectionFailureReasonDNSLookup;
    case Service::kFailureEAPAuthentication:
      return Metrics::kUserInitiatedConnectionFailureReasonEAPAuthentication;
    case Service::kFailureEAPLocalTLS:
      return Metrics::kUserInitiatedConnectionFailureReasonEAPLocalTLS;
    case Service::kFailureEAPRemoteTLS:
      return Metrics::kUserInitiatedConnectionFailureReasonEAPRemoteTLS;
    case Service::kFailureNotAssociated:
      return Metrics::kUserInitiatedConnectionFailureReasonNotAssociated;
    case Service::kFailureNotAuthenticated:
      return Metrics::kUserInitiatedConnectionFailureReasonNotAuthenticated;
    case Service::kFailureOutOfRange:
      return Metrics::kUserInitiatedConnectionFailureReasonOutOfRange;
    case Service::kFailurePinMissing:
      return Metrics::kUserInitiatedConnectionFailureReasonPinMissing;
    case Service::kFailureTooManySTAs:
      return Metrics::kUserInitiatedConnectionFailureReasonTooManySTAs;
    default:
      return Metrics::kUserInitiatedConnectionFailureReasonUnknown;
  }
}

std::string Service::GetTechnologyName() const {
  return TechnologyName(technology());
}

bool Service::ShouldIgnoreFailure() const {
  // Ignore the event if it's user-initiated explicit disconnect.
  if (explicitly_disconnected_) {
    SLOG(2) << *this << " " << __func__ << ": Explicit disconnect ignored.";
    return true;
  }
  // Ignore the event if manager is not running (e.g., service disconnects on
  // shutdown).
  if (!manager_->running()) {
    SLOG(2) << *this << " " << __func__
            << ": Disconnect while manager stopped ignored.";
    return true;
  }
  // Ignore the event if the system is suspending.
  // TODO(b/179949996): This is racy because the failure event isn't guaranteed
  // to come before PowerManager::OnSuspendDone().
  PowerManager* power_manager = manager_->power_manager();
  if (!power_manager || power_manager->suspending()) {
    SLOG(2) << *this << " " << __func__
            << ": Disconnect in transitional power state ignored.";
    return true;
  }
  return false;
}

void Service::NoteFailureEvent() {
  SLOG(2) << *this << " " << __func__;
  if (ShouldIgnoreFailure()) {
    return;
  }
  int period = 0;
  EventHistory* events = nullptr;
  // Sometimes services transition to Idle before going into a failed state so
  // take into account the last non-idle state.
  ConnectState state = state_ == kStateIdle ? previous_state_ : state_;
  if (IsConnectedState(state)) {
    LOG(INFO) << *this << " " << __func__ << ": Unexpected connection drop";
    period = kDisconnectsMonitorDuration.InSeconds();
    events = &disconnects_;
  } else if (IsConnectingState(state)) {
    LOG(INFO) << *this << " " << __func__ << ": Unexpected failure to connect";
    period = kMisconnectsMonitorDuration.InSeconds();
    events = &misconnects_;
  } else {
    SLOG(2) << *this << " " << __func__
            << ": Not connected or connecting, state transition ignored.";
    return;
  }
  events->RecordEventAndExpireEventsBefore(period,
                                           EventHistory::kClockTypeMonotonic);
}

void Service::ReportUserInitiatedConnectionResult(ConnectState state) {
  // Report stats for wifi only for now.
  if (technology_ != Technology::kWiFi) {
    return;
  }

  int result;
  switch (state) {
    case kStateConnected:
      result = Metrics::kUserInitiatedConnectionResultSuccess;
      break;
    case kStateFailure:
      result = Metrics::kUserInitiatedConnectionResultFailure;
      metrics()->SendEnumToUMA(
          Metrics::kMetricWifiUserInitiatedConnectionFailureReason,
          ConnectFailureToFailureReason(failure_));
      break;
    case kStateIdle:
      // This assumes the device specific class (wifi, cellular) will advance
      // the service's state from idle to other state after connection attempt
      // is initiated for the given service.
      result = Metrics::kUserInitiatedConnectionResultAborted;
      break;
    default:
      return;
  }

  metrics()->SendEnumToUMA(Metrics::kMetricWifiUserInitiatedConnectionResult,
                           result);
}

bool Service::HasRecentConnectionIssues() {
  disconnects_.ExpireEventsBefore(kDisconnectsMonitorDuration.InSeconds(),
                                  EventHistory::kClockTypeMonotonic);
  misconnects_.ExpireEventsBefore(kMisconnectsMonitorDuration.InSeconds(),
                                  EventHistory::kClockTypeMonotonic);
  return !disconnects_.Empty() || !misconnects_.Empty();
}

// static
bool Service::DecideBetween(int a, int b, bool* decision) {
  if (a == b) {
    return false;
  }
  *decision = (a > b);
  return true;
}

uint16_t Service::SecurityLevel() {
  return (crypto_algorithm_ << 2) | (key_rotation_ << 1) | endpoint_auth_;
}

bool Service::IsMetered() const {
  if (metered_override_.has_value()) {
    return metered_override_.value();
  }

  if (IsMeteredByServiceProperties()) {
    return true;
  }

  TetheringState tethering = GetTethering();
  return tethering == TetheringState::kSuspected ||
         tethering == TetheringState::kConfirmed;
}

bool Service::IsMeteredByServiceProperties() const {
  return false;
}

void Service::InitializeTrafficCounterSnapshots(
    const Network::TrafficCounterMap& network_raw_counters,
    const Network::TrafficCounterMap& extra_raw_counters) {
  total_traffic_counter_snapshot_ = current_total_traffic_counters_;
  network_raw_traffic_counter_snapshot_ = network_raw_counters;
  extra_raw_traffic_counter_snapshot_ = extra_raw_counters;
}

void Service::RefreshTrafficCounters(
    const Network::TrafficCounterMap& network_raw_counters,
    const Network::TrafficCounterMap& extra_raw_counters) {
  Network::TrafficCounterMap network_delta = Network::DiffTrafficCounters(
      network_raw_counters, network_raw_traffic_counter_snapshot_);
  Network::TrafficCounterMap extra_delta = Network::DiffTrafficCounters(
      extra_raw_counters, extra_raw_traffic_counter_snapshot_);
  Network::TrafficCounterMap total_delta =
      Network::AddTrafficCounters(network_delta, extra_delta);
  current_total_traffic_counters_ =
      Network::AddTrafficCounters(total_traffic_counter_snapshot_, total_delta);

  SaveToProfile();
}

void Service::GetTrafficCounters(ResultVariantDictionariesCallback callback) {
  std::vector<brillo::VariantDictionary> traffic_counters;
  for (const auto& [source, traffic] : current_total_traffic_counters_) {
    brillo::VariantDictionary dict;
    // Only export rx_bytes and tx_bytes.
    dict.emplace("source", patchpanel::Client::TrafficSourceName(source));
    dict.emplace("rx_bytes", traffic.rx_bytes);
    dict.emplace("tx_bytes", traffic.tx_bytes);
    traffic_counters.push_back(std::move(dict));
  }
  std::move(callback).Run(Error(Error::kSuccess), std::move(traffic_counters));
}

void Service::RequestTrafficCountersCallback(
    ResultVariantDictionariesCallback callback,
    const Network::TrafficCounterMap& raw_counters,
    const Network::TrafficCounterMap& extra_raw_counters) {
  RefreshTrafficCounters(raw_counters, extra_raw_counters);
  GetTrafficCounters(std::move(callback));
}

void Service::RequestTrafficCounters(
    ResultVariantDictionariesCallback callback) {
  LOG(INFO) << *this << " " << __func__;

  // When the Service has no attached Network, reply with the current traffic
  // counters.
  if (!attached_network_) {
    LOG(INFO) << *this << " " << __func__
              << ": No attached network, pass the current counters directly";
    GetTrafficCounters(std::move(callback));
    return;
  }

  // Otherwise update the raw traffic counter snapshot and reply with the
  // refreshed traffic counters. This only takes into account the main Network
  // of this Service. Any technology specific Service with additional secondary
  // Networks must query traffic counters for these networks separately.
  RequestRawTrafficCounters(
      base::BindOnce(&Service::RequestTrafficCountersCallback,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void Service::ResetTrafficCounters(Error* /*error*/) {
  // Any raw snapshot also need to be reset to the current value which requires
  // an async query. To avoid inconsistency change this function to do the
  // reinitialization asynchronously (without waiting here and without a
  // callback).
  RequestRawTrafficCounters(base::BindOnce(
      &Service::ResetTrafficCountersCallback, weak_ptr_factory_.GetWeakPtr()));
}

void Service::ResetTrafficCountersCallback(
    const Network::TrafficCounterMap& raw_counters,
    const Network::TrafficCounterMap& extra_raw_counters) {
  LOG(INFO) << *this << " " << __func__;
  current_total_traffic_counters_.clear();
  total_traffic_counter_snapshot_.clear();
  network_raw_traffic_counter_snapshot_ = raw_counters;
  extra_raw_traffic_counter_snapshot_ = extra_raw_counters;
  traffic_counter_reset_time_ = base::Time::Now();
  SaveToProfile();
}

void Service::RefreshTrafficCountersTask(bool initialize) {
  if (initialize) {
    RequestRawTrafficCounters(
        base::BindOnce(&Service::InitializeTrafficCounterSnapshots,
                       weak_ptr_factory_.GetWeakPtr()));
  } else {
    RequestRawTrafficCounters(base::BindOnce(&Service::RefreshTrafficCounters,
                                             weak_ptr_factory_.GetWeakPtr()));
  }
  refresh_traffic_counter_task_.Reset(
      base::BindOnce(&Service::RefreshTrafficCountersTask,
                     weak_ptr_factory_.GetWeakPtr(), /*initialize=*/false));
  dispatcher()->PostDelayedTask(FROM_HERE,
                                refresh_traffic_counter_task_.callback(),
                                kTrafficCountersRefreshInterval);
}

void Service::RequestRawTrafficCounters(
    RequestRawTrafficCountersCallback callback) {
  if (!attached_network_) {
    LOG(WARNING) << *this << " " << __func__ << ": No attached network";
    return;
  }

  attached_network_->RequestTrafficCounters(
      base::BindOnce(&Service::RequestExtraRawTrafficCounters,
                     weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
}

void Service::RequestExtraRawTrafficCounters(
    RequestRawTrafficCountersCallback callback,
    const Network::TrafficCounterMap& network_raw_counters) {
  GetExtraTrafficCounters(
      base::BindOnce(std::move(callback), network_raw_counters));
}

bool Service::CompareWithSameTechnology(const ServiceRefPtr& service,
                                        bool* decision) {
  return false;
}

// static
std::string Service::GetCurrentTrafficCounterKey(
    patchpanel::Client::TrafficSource source, std::string suffix) {
  return patchpanel::Client::TrafficSourceName(source) + suffix;
}

// static
std::pair<bool, const char*> Service::Compare(
    ServiceRefPtr a,
    ServiceRefPtr b,
    bool compare_connectivity_state,
    const std::vector<Technology>& tech_order) {
  CHECK_EQ(a->manager(), b->manager());
  bool ret;

  if (compare_connectivity_state && a->state() != b->state()) {
    if (DecideBetween(a->IsOnline(), b->IsOnline(), &ret)) {
      return std::make_pair(ret, kServiceSortIsOnline);
    }

    if (DecideBetween(a->IsConnected(), b->IsConnected(), &ret)) {
      return std::make_pair(ret, kServiceSortIsConnected);
    }

    if (DecideBetween(!a->IsPortalled(), !b->IsPortalled(), &ret)) {
      return std::make_pair(ret, kServiceSortIsPortalled);
    }

    if (DecideBetween(a->IsConnecting(), b->IsConnecting(), &ret)) {
      return std::make_pair(ret, kServiceSortIsConnecting);
    }

    if (DecideBetween(!a->IsFailed(), !b->IsFailed(), &ret)) {
      return std::make_pair(ret, kServiceSortIsFailed);
    }
  }

  if (DecideBetween(a->connectable(), b->connectable(), &ret)) {
    return std::make_pair(ret, kServiceSortConnectable);
  }

  for (auto technology : tech_order) {
    if (DecideBetween(a->technology() == technology,
                      b->technology() == technology, &ret)) {
      return std::make_pair(ret, kServiceSortTechnology);
    }
  }

  if (DecideBetween(a->ephemeral_priority_, b->ephemeral_priority_, &ret)) {
    return std::make_pair(ret, kServiceSortPriority);
  }

  if (DecideBetween(a->priority(), b->priority(), &ret)) {
    return std::make_pair(ret, kServiceSortPriority);
  }

  if (DecideBetween(a->SourcePriority(), b->SourcePriority(), &ret)) {
    return std::make_pair(ret, kServiceSortSource);
  }

  if (DecideBetween(a->managed_credentials_, b->managed_credentials_, &ret)) {
    return std::make_pair(ret, kServiceSortManagedCredentials);
  }

  if (DecideBetween(a->auto_connect(), b->auto_connect(), &ret)) {
    return std::make_pair(ret, kServiceSortAutoConnect);
  }

  if (DecideBetween(a->SecurityLevel(), b->SecurityLevel(), &ret)) {
    return std::make_pair(ret, kServiceSortSecurity);
  }

  // If the profiles for the two services are different,
  // we want to pick the highest priority one.  The
  // ephemeral profile is explicitly tested for since it is not
  // listed in the manager profiles_ list.
  if (a->profile() != b->profile()) {
    Manager* manager = a->manager();
    ret = manager->IsServiceEphemeral(b) ||
          (!manager->IsServiceEphemeral(a) &&
           manager->IsProfileBefore(b->profile(), a->profile()));
    return std::make_pair(ret, kServiceSortProfileOrder);
  }

  if (DecideBetween(a->has_ever_connected(), b->has_ever_connected(), &ret)) {
    return std::make_pair(ret, kServiceSortHasEverConnected);
  }

  if (a->CompareWithSameTechnology(b, &ret)) {
    return std::make_pair(ret, kServiceSortTechnologySpecific);
  }

  if (DecideBetween(a->strength(), b->strength(), &ret)) {
    return std::make_pair(ret, kServiceSortEtc);
  }

  ret = a->serial_number_ < b->serial_number_;
  return std::make_pair(ret, kServiceSortSerialNumber);
}

// static
std::string Service::SanitizeStorageIdentifier(std::string identifier) {
  std::replace_if(
      identifier.begin(), identifier.end(),
      [](unsigned char c) { return !std::isalnum(c); }, '_');
  return identifier;
}

const ProfileRefPtr& Service::profile() const {
  return profile_;
}

void Service::set_profile(const ProfileRefPtr& p) {
  profile_ = p;
}

void Service::SetProfile(const ProfileRefPtr& p) {
  SLOG(2) << *this << " " << __func__ << ": From "
          << (profile_ ? profile_->GetFriendlyName() : "(none)") << " to "
          << (p ? p->GetFriendlyName() : "(none)") << ".";
  if (profile_ == p) {
    return;
  }
  profile_ = p;
  Error error;
  std::string profile_rpc_id = GetProfileRpcId(&error);
  if (!error.IsSuccess()) {
    return;
  }
  adaptor_->EmitStringChanged(kProfileProperty, profile_rpc_id);
}

void Service::OnPropertyChanged(std::string_view property) {
  SLOG(1) << *this << " " << __func__ << ": " << property;
  if (Is8021x() && EapCredentials::IsEapAuthenticationProperty(property)) {
    OnEapCredentialsChanged(kReasonPropertyUpdate);
  }
  SaveToProfile();
  if (property == kStaticIPConfigProperty) {
    NotifyStaticIPConfigChanged();
  }
  if (!IsConnected()) {
    return;
  }

  if (property == kPriorityProperty || property == kEphemeralPriorityProperty ||
      property == kManagedCredentialsProperty) {
    // These properties affect the sorting order of Services. Note that this is
    // only necessary if there are multiple connected Services that would be
    // sorted differently by this change, so we can avoid doing this for
    // unconnected Services.
    manager_->SortServices();
  }
}

void Service::OnBeforeSuspend(ResultCallback callback) {
  // Nothing to be done in the general case, so immediately report success.
  std::move(callback).Run(Error(Error::kSuccess));
}

void Service::OnAfterResume() {
  time_resume_to_ready_timer_->Start();
  // Forget old autoconnect failures across suspend/resume.
  ResetAutoConnectCooldownTime();
  // Forget if the user disconnected us, we might be able to connect now.
  ClearExplicitlyDisconnected();
}

void Service::OnDarkResume() {
  // Nothing to do in the general case.
}

void Service::OnDefaultServiceStateChanged(const ServiceRefPtr& parent) {
  // Nothing to do in the general case.
}

RpcIdentifier Service::GetIPConfigRpcIdentifier(Error* error) const {
  IPConfig* ipconfig = nullptr;
  if (attached_network_) {
    ipconfig = attached_network_->GetCurrentIPConfig();
  }
  if (!ipconfig) {
    // Do not return an empty IPConfig.
    error->Populate(Error::kNotFound);
    return DBusControl::NullRpcIdentifier();
  }
  return ipconfig->GetRpcIdentifier();
}

void Service::SetConnectable(bool connectable) {
  if (connectable_ == connectable) {
    return;
  }
  connectable_ = connectable;
  adaptor_->EmitBoolChanged(kConnectableProperty, connectable_);
}

void Service::SetConnectableFull(bool connectable) {
  if (connectable_ == connectable) {
    return;
  }
  SetConnectable(connectable);
  if (manager_->HasService(this)) {
    manager_->UpdateService(this);
  }
}

std::string Service::GetStateString() const {
  // TODO(benchan): We may want to rename shill::kState* to avoid name clashing
  // with Service::kState*.
  switch (state()) {
    case kStateIdle:
      return shill::kStateIdle;
    case kStateAssociating:
      return shill::kStateAssociation;
    case kStateConfiguring:
      return shill::kStateConfiguration;
    case kStateConnected:
      return shill::kStateReady;
    case kStateFailure:
      return shill::kStateFailure;
    case kStateNoConnectivity:
      return shill::kStateNoConnectivity;
    case kStateRedirectFound:
      return shill::kStateRedirectFound;
    case kStateOnline:
      return shill::kStateOnline;
    case kStateDisconnecting:
      return shill::kStateDisconnecting;
    case kStateUnknown:
    default:
      return "";
  }
}

bool Service::IsAutoConnectable(const char** reason) const {
  if (manager_->IsTechnologyAutoConnectDisabled(technology_)) {
    *reason = kAutoConnTechnologyNotAutoConnectable;
    return false;
  }

  if (!connectable()) {
    *reason = kAutoConnNotConnectable;
    return false;
  }

  if (IsConnected()) {
    *reason = kAutoConnConnected;
    return false;
  }

  if (IsConnecting()) {
    *reason = kAutoConnConnecting;
    return false;
  }

  if (IsDisconnecting()) {
    *reason = kAutoConnDisconnecting;
    return false;
  }

  if (explicitly_disconnected_) {
    *reason = kAutoConnExplicitDisconnect;
    return false;
  }

  if (!reenable_auto_connect_task_.IsCancelled()) {
    *reason = kAutoConnThrottled;
    return false;
  }

  if (!IsPrimaryConnectivityTechnology(technology_) &&
      !manager_->IsConnected()) {
    *reason = kAutoConnOffline;
    return false;
  }

  // It's possible for a connection failure to trigger an autoconnect to the
  // same Service. This happens with no cooldown, so we'll see a connection
  // failure immediately followed by an autoconnect attempt. This is desirable
  // in many cases (e.g. there's a brief AP-/network-side issue), but not when
  // the failure is due to a bad passphrase. Enforce a minimum cooldown time to
  // avoid this.
  auto time_since_failed = GetTimeSinceFailed();
  if (time_since_failed &&
      time_since_failed.value() < kMinAutoConnectCooldownTime &&
      previous_error_ == kErrorBadPassphrase) {
    *reason = kAutoConnRecentBadPassphraseFailure;
    return false;
  }

  return true;
}

base::TimeDelta Service::GetMinAutoConnectCooldownTime() const {
  return kMinAutoConnectCooldownTime;
}

base::TimeDelta Service::GetMaxAutoConnectCooldownTime() const {
  return kMaxAutoConnectCooldownTime;
}

bool Service::IsDisconnectable(Error* error) const {
  if (!IsActive(nullptr)) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kNotConnected,
        base::StringPrintf("Disconnect attempted but Service is not active: %s",
                           log_name().c_str()));
    return false;
  }
  return true;
}

NetworkMonitor::ValidationMode Service::GetNetworkValidationMode() {
  switch (check_portal_) {
    case CheckPortalState::kTrue:
      return NetworkMonitor::ValidationMode::kFullValidation;
    case CheckPortalState::kFalse:
      return NetworkMonitor::ValidationMode::kDisabled;
    case CheckPortalState::kHTTPOnly:
      return NetworkMonitor::ValidationMode::kHTTPOnly;
    case CheckPortalState::kAutomatic:
      // ValidateMode specified by the technology should have higher priority
      // than the inferred value from other fields.
      if (!manager_->IsPortalDetectionEnabled(technology())) {
        return NetworkMonitor::ValidationMode::kDisabled;
      }

      // b/279520395: Network validation should not run by default on Services
      // created through policies which most of the time represent on-prem
      // networks:
      //   - The firewall of the network may reject HTTPS validation probes.
      //   - The platform is not aware of the global HTTP proxy configuration
      //   that exists in Chrome to go through the firewall.
      if (source_ == ONCSource::kONCSourceDevicePolicy ||
          source_ == ONCSource::kONCSourceUserPolicy) {
        return NetworkMonitor::ValidationMode::kDisabled;
      }

      // When the Service itself has an explicit proxy configuration (manual
      // configuration or PAC URL configuration), network validation is set by
      // default to "http-only" to ensure that an on-prem strict firewalls do
      // not block the HTTPS probes and prevent the Service from transitioning
      // to the "online" state. Captive portal HTTP detection probes can still
      // be sent because the firewall will be able to intercept them and reply
      // to them explicitly. See b/302126338.
      //
      // In most cases, the proxy configuration is set by the user for accessing
      // the Internet in the browser through a remote web proxy. In these cases,
      // the "http-only" allows to detect captive portals.
      if (HasProxyConfig()) {
        return NetworkMonitor::ValidationMode::kHTTPOnly;
      }

      return NetworkMonitor::ValidationMode::kFullValidation;
  }
}

void Service::HelpRegisterDerivedBool(std::string_view name,
                                      bool (Service::*get)(Error* error),
                                      bool (Service::*set)(const bool&, Error*),
                                      void (Service::*clear)(Error*)) {
  store_.RegisterDerivedBool(
      name,
      BoolAccessor(new CustomAccessor<Service, bool>(this, get, set, clear)));
}

void Service::HelpRegisterDerivedInt32(std::string_view name,
                                       int32_t (Service::*get)(Error* error),
                                       bool (Service::*set)(const int32_t&,
                                                            Error*)) {
  store_.RegisterDerivedInt32(
      name,
      Int32Accessor(new CustomAccessor<Service, int32_t>(this, get, set)));
}

void Service::HelpRegisterDerivedString(
    std::string_view name,
    std::string (Service::*get)(Error* error),
    bool (Service::*set)(const std::string&, Error*)) {
  store_.RegisterDerivedString(
      name,
      StringAccessor(new CustomAccessor<Service, std::string>(this, get, set)));
}

void Service::HelpRegisterConstDerivedRpcIdentifier(
    std::string_view name, RpcIdentifier (Service::*get)(Error*) const) {
  store_.RegisterDerivedRpcIdentifier(
      name, RpcIdentifierAccessor(
                new CustomReadOnlyAccessor<Service, RpcIdentifier>(this, get)));
}

void Service::HelpRegisterConstDerivedStrings(
    std::string_view name, Strings (Service::*get)(Error* error) const) {
  store_.RegisterDerivedStrings(
      name,
      StringsAccessor(new CustomReadOnlyAccessor<Service, Strings>(this, get)));
}

void Service::HelpRegisterConstDerivedString(
    std::string_view name, std::string (Service::*get)(Error* error) const) {
  store_.RegisterDerivedString(
      name, StringAccessor(
                new CustomReadOnlyAccessor<Service, std::string>(this, get)));
}

void Service::HelpRegisterConstDerivedUint64(
    std::string_view name, uint64_t (Service::*get)(Error* error) const) {
  store_.RegisterDerivedUint64(
      name,
      Uint64Accessor(new CustomReadOnlyAccessor<Service, uint64_t>(this, get)));
}

void Service::HelpRegisterConstDerivedInt32(
    std::string_view name, int32_t (Service::*get)(Error* error) const) {
  store_.RegisterDerivedInt32(
      name,
      Int32Accessor(new CustomReadOnlyAccessor<Service, int32_t>(this, get)));
}

// static
void Service::LoadString(const StoreInterface* storage,
                         const std::string& id,
                         const std::string& key,
                         const std::string& default_value,
                         std::string* value) {
  if (!storage->GetString(id, key, value)) {
    *value = default_value;
  }
}

// static
void Service::SaveStringOrClear(StoreInterface* storage,
                                const std::string& id,
                                const std::string& key,
                                const std::string& value) {
  if (value.empty()) {
    storage->DeleteKey(id, key);
    return;
  }
  storage->SetString(id, key, value);
}

// static
void Service::SetNextSerialNumberForTesting(unsigned int next_serial_number) {
  next_serial_number_ = next_serial_number;
}

std::map<RpcIdentifier, std::string> Service::GetLoadableProfileEntries() {
  return manager_->GetLoadableProfileEntriesForService(this);
}

std::string Service::CalculateState(Error* /*error*/) {
  return GetStateString();
}

std::string Service::CalculateTechnology(Error* /*error*/) {
  return GetTechnologyName();
}

Service::TetheringState Service::GetTethering() const {
  return TetheringState::kUnknown;
}

void Service::IgnoreParameterForConfigure(const std::string& parameter) {
  parameters_ignored_for_configure_.insert(parameter);
}

const std::string& Service::GetEAPKeyManagement() const {
  CHECK(eap());
  return eap()->key_management();
}

void Service::SetEAPKeyManagement(const std::string& key_management) {
  CHECK(mutable_eap());
  mutable_eap()->SetKeyManagement(key_management, nullptr);
}

bool Service::GetAutoConnect(Error* /*error*/) {
  return auto_connect();
}

bool Service::SetAutoConnectFull(const bool& connect, Error* /*error*/) {
  LOG(INFO) << *this << " " << __func__ << ": AutoConnect=" << auto_connect()
            << "->" << connect;
  if (!retain_auto_connect_) {
    RetainAutoConnect();
    // Irrespective of an actual change in the |kAutoConnectProperty|, we must
    // flush the current value of the property to the profile.
    if (IsRemembered()) {
      SaveToProfile();
    }
  }

  if (auto_connect() == connect) {
    return false;
  }

  SetAutoConnect(connect);
  manager_->UpdateService(this);
  return true;
}

void Service::ClearAutoConnect(Error* /*error*/) {
  if (auto_connect()) {
    SetAutoConnect(false);
    manager_->UpdateService(this);
  }

  retain_auto_connect_ = false;
}

std::string Service::GetCheckPortal(Error* error) {
  return CheckPortalStateToString(check_portal_);
}

bool Service::SetCheckPortal(const std::string& check_portal_name,
                             Error* error) {
  const std::optional<CheckPortalState> check_portal =
      CheckPortalStateFromString(check_portal_name);
  if (!check_portal) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kInvalidArguments,
        "Invalid Service CheckPortal property value: " + check_portal_name);
    return false;
  }

  if (*check_portal == check_portal_) {
    return false;
  }
  LOG(INFO) << *this << " " << __func__ << ": "
            << CheckPortalStateToString(check_portal_) << " -> "
            << CheckPortalStateToString(*check_portal);
  check_portal_ = *check_portal;
  UpdateNetworkValidationMode();
  return true;
}

std::string Service::GetGuid(Error* error) {
  return guid_;
}

bool Service::SetGuid(const std::string& guid, Error* /*error*/) {
  if (guid_ == guid) {
    return false;
  }
  guid_ = guid;
  adaptor_->EmitStringChanged(kGuidProperty, guid_);
  return true;
}

void Service::RetainAutoConnect() {
  retain_auto_connect_ = true;
}

void Service::SetSecurity(CryptoAlgorithm crypto_algorithm,
                          bool key_rotation,
                          bool endpoint_auth) {
  crypto_algorithm_ = crypto_algorithm;
  key_rotation_ = key_rotation;
  endpoint_auth_ = endpoint_auth;
}

std::string Service::GetNameProperty(Error* /*error*/) {
  return friendly_name_;
}

bool Service::SetNameProperty(const std::string& name, Error* error) {
  if (name != friendly_name_) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kInvalidArguments,
        base::StringPrintf("Service %s Name property cannot be modified.",
                           log_name_.c_str()));
    return false;
  }
  return false;
}

void Service::SetHasEverConnected(bool has_ever_connected) {
  if (has_ever_connected_ == has_ever_connected) {
    return;
  }
  has_ever_connected_ = has_ever_connected;
}

int32_t Service::GetPriority(Error* error) {
  return priority_;
}

bool Service::SetPriority(const int32_t& priority, Error* error) {
  if (priority_ == priority) {
    return false;
  }
  priority_ = priority;
  adaptor_->EmitIntChanged(kPriorityProperty, priority_);
  return true;
}

std::string Service::GetProfileRpcId(Error* error) {
  if (!profile_) {
    // This happens in some unit tests where profile_ is not set.
    error->Populate(Error::kNotFound);
    return RpcIdentifier().value();
  }
  return profile_->GetRpcIdentifier().value();
}

bool Service::SetProfileRpcId(const std::string& profile, Error* error) {
  if (profile_ && profile_->GetRpcIdentifier().value() == profile) {
    return false;
  }
  ProfileConstRefPtr old_profile = profile_;
  // No need to Emit afterwards, since SetProfileForService will call
  // into SetProfile (if the profile actually changes).
  manager_->SetProfileForService(this, profile, error);
  // Can't just use error.IsSuccess(), because that also requires saving
  // the profile to succeed. (See Profile::AdoptService)
  return (profile_ != old_profile);
}

std::string Service::GetProxyConfig(Error* error) {
  return proxy_config_;
}

bool Service::SetProxyConfig(const std::string& proxy_config, Error* error) {
  if (proxy_config_ == proxy_config) {
    return false;
  }
  proxy_config_ = proxy_config;
  // Force network validation to restart if it was already running: the new
  // Proxy settings could change validation results.
  LOG(INFO)
      << *this << " " << __func__
      << ": Restarting network validation after proxy configuration change";
  UpdateNetworkValidationMode();
  adaptor_->EmitStringChanged(kProxyConfigProperty, proxy_config_);
  return true;
}

void Service::NotifyIfVisibilityChanged() {
  const bool is_visible = IsVisible();
  if (was_visible_ != is_visible) {
    adaptor_->EmitBoolChanged(kVisibleProperty, is_visible);
  }
  was_visible_ = is_visible;
}

Strings Service::GetDisconnectsProperty(Error* /*error*/) const {
  return disconnects_.ExtractWallClockToStrings();
}

Strings Service::GetMisconnectsProperty(Error* /*error*/) const {
  return misconnects_.ExtractWallClockToStrings();
}

uint64_t Service::GetTrafficCounterResetTimeProperty(Error* /*error*/) const {
  return traffic_counter_reset_time_.ToDeltaSinceWindowsEpoch()
      .InMilliseconds();
}

void Service::SetLastManualConnectAttemptProperty(const base::Time& value) {
  if (last_manual_connect_attempt_ == value) {
    return;
  }
  last_manual_connect_attempt_ = value;
  if (technology_ == Technology::kCellular) {
    manager_->power_opt()->UpdateManualConnectTime(
        last_manual_connect_attempt_);
  }
  adaptor_->EmitUint64Changed(kLastManualConnectAttemptProperty,
                              GetLastManualConnectAttemptProperty(nullptr));
}

uint64_t Service::GetLastManualConnectAttemptProperty(Error* /*error*/) const {
  return last_manual_connect_attempt_.ToDeltaSinceWindowsEpoch()
      .InMilliseconds();
}

void Service::SetLastConnectedProperty(const base::Time& value) {
  if (last_connected_ == value) {
    return;
  }
  last_connected_ = value;
  adaptor_->EmitUint64Changed(kLastConnectedProperty,
                              GetLastConnectedProperty(nullptr));
}

uint64_t Service::GetLastConnectedProperty(Error* /*error*/) const {
  return last_connected_.ToDeltaSinceWindowsEpoch().InMilliseconds();
}

void Service::SetLastOnlineProperty(const base::Time& value) {
  if (last_online_ == value) {
    return;
  }
  last_online_ = value;
  adaptor_->EmitUint64Changed(kLastOnlineProperty,
                              GetLastOnlineProperty(nullptr));
}

uint64_t Service::GetLastOnlineProperty(Error* /*error*/) const {
  return last_online_.ToDeltaSinceWindowsEpoch().InMilliseconds();
}

void Service::SetStartTimeProperty(const base::Time& value) {
  if (start_time_ == value) {
    return;
  }
  start_time_ = value;
  adaptor_->EmitUint64Changed(kStartTimeProperty,
                              GetStartTimeProperty(nullptr));
}

uint64_t Service::GetStartTimeProperty(Error* /*error*/) const {
  return start_time_.ToDeltaSinceWindowsEpoch().InMilliseconds();
}

int32_t Service::GetNetworkID(Error* /*error*/) const {
  if (!attached_network_) {
    return 0;
  }
  return attached_network_->network_id();
}

bool Service::GetMeteredProperty(Error* /*error*/) {
  return IsMetered();
}

bool Service::SetMeteredProperty(const bool& metered, Error* /*error*/) {
  // We always want to set the override, but only emit a signal if
  // the value has actually changed as a result.
  bool was_metered = IsMetered();
  metered_override_ = metered;

  if (was_metered == metered) {
    return false;
  }
  adaptor_->EmitBoolChanged(kMeteredProperty, metered);
  return true;
}

void Service::ClearMeteredProperty(Error* /*error*/) {
  bool was_metered = IsMetered();
  metered_override_ = std::nullopt;

  bool is_metered = IsMetered();
  if (was_metered != is_metered) {
    adaptor_->EmitBoolChanged(kMeteredProperty, is_metered);
  }
}

std::string Service::GetONCSource(Error* error) {
  if (base::to_underlying(source_) >= kONCSourceMapping.size()) {
    LOG(WARNING) << *this << " " << __func__
                 << ": Bad source value: " << base::to_underlying(source_);
    return kONCSourceUnknown;
  }

  return kONCSourceMapping[base::to_underlying(source_)];
}

bool Service::SetONCSource(const std::string& source, Error* error) {
  if (kONCSourceMapping[base::to_underlying(source_)] == source) {
    return false;
  }
  auto it =
      std::find(kONCSourceMapping.begin(), kONCSourceMapping.end(), source);
  if (it == kONCSourceMapping.end()) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kInvalidArguments,
        base::StringPrintf("Service %s: Source property value %s invalid.",
                           log_name_.c_str(), source.c_str()));
    return false;
  }
  source_ =
      static_cast<ONCSource>(std::distance(kONCSourceMapping.begin(), it));
  adaptor_->EmitStringChanged(kONCSourceProperty,
                              kONCSourceMapping[base::to_underlying(source_)]);
  return true;
}

int Service::SourcePriority() {
  static constexpr std::array<Service::ONCSource,
                              base::to_underlying(
                                  Service::ONCSource::kONCSourcesNum)>
      priorities = {Service::ONCSource::kONCSourceUnknown,
                    Service::ONCSource::kONCSourceNone,
                    Service::ONCSource::kONCSourceUserImport,
                    Service::ONCSource::kONCSourceDevicePolicy,
                    Service::ONCSource::kONCSourceUserPolicy};

  auto it = std::find(priorities.begin(), priorities.end(), Source());
  DCHECK(it != priorities.end());
  return std::distance(priorities.begin(), it);
}

bool Service::GetVisibleProperty(Error* /*error*/) {
  return IsVisible();
}

void Service::SaveToProfile() {
  if (profile_.get() && profile_->GetConstStorage()) {
    profile_->UpdateService(this);
  }
}

void Service::SetFriendlyName(const std::string& friendly_name) {
  if (friendly_name == friendly_name_) {
    return;
  }
  friendly_name_ = friendly_name;
  adaptor()->EmitStringChanged(kNameProperty, friendly_name_);
}

void Service::SetStrength(uint8_t strength) {
  if (strength == strength_) {
    return;
  }
  strength_ = strength;
  adaptor_->EmitUint8Changed(kSignalStrengthProperty, strength);
}

void Service::SetErrorDetails(std::string_view details) {
  if (error_details_ == details) {
    return;
  }
  error_details_ = std::string(details);
  adaptor_->EmitStringChanged(kErrorDetailsProperty, error_details_);
}

void Service::UpdateErrorProperty() {
  const std::string error(ConnectFailureToString(failure_));
  if (error == error_) {
    return;
  }
  LOG(INFO) << *this << " " << __func__ << ": " << error;
  error_ = error;
  adaptor_->EmitStringChanged(kErrorProperty, error);
}

void Service::ClearExplicitlyDisconnected() {
  if (explicitly_disconnected_) {
    explicitly_disconnected_ = false;
    manager_->UpdateService(this);
  }
}

EventDispatcher* Service::dispatcher() const {
  return manager_->dispatcher();
}

Metrics* Service::metrics() const {
  return manager_->metrics();
}

void Service::SetUplinkSpeedKbps(uint32_t uplink_speed_kbps) {
  if (uplink_speed_kbps != uplink_speed_kbps_) {
    uplink_speed_kbps_ = uplink_speed_kbps;
    adaptor_->EmitIntChanged(kUplinkSpeedPropertyKbps, uplink_speed_kbps_);
  }
}

void Service::SetDownlinkSpeedKbps(uint32_t downlink_speed_kbps) {
  if (downlink_speed_kbps != downlink_speed_kbps_) {
    downlink_speed_kbps_ = downlink_speed_kbps;
    adaptor_->EmitIntChanged(kDownlinkSpeedPropertyKbps, downlink_speed_kbps_);
  }
}

void Service::UpdateStateTransitionMetrics(Service::ConnectState new_state) {
  UpdateServiceStateTransitionMetrics(new_state);
  if (new_state == kStateFailure) {
    Metrics::NetworkServiceError error = ConnectFailureToMetricsEnum(failure());
    // Publish technology specific connection failure metrics. This will
    // account for all the connection failures happening while connected to
    // a particular interface e.g. wifi, cellular etc.
    metrics()->SendEnumToUMA(Metrics::kMetricNetworkServiceError, technology(),
                             error);
  }
  bootstat::BootStat().LogEvent(
      base::StrCat({"network-", GetTechnologyName(), "-", GetStateString()}));
  if (new_state != kStateConnected) {
    return;
  }
  base::TimeDelta time_resume_to_ready;
  time_resume_to_ready_timer_->GetElapsedTime(&time_resume_to_ready);
  time_resume_to_ready_timer_->Reset();
  SendPostReadyStateMetrics(time_resume_to_ready);
}

void Service::UpdateServiceStateTransitionMetrics(
    Service::ConnectState new_state) {
  const char* state_string = ConnectStateToString(new_state);
  SLOG(5) << *this << " " << __func__ << ": new_state=" << state_string;
  TimerReportersList& start_timers =
      service_metrics_->start_on_state[new_state];
  for (auto* start_timer : start_timers) {
    SLOG(5) << *this << " " << __func__ << ": Starting timer for "
            << start_timer->histogram_name() << " due to new state "
            << state_string << ".";
    start_timer->Start();
  }
  TimerReportersList& stop_timers = service_metrics_->stop_on_state[new_state];
  for (auto* stop_timer : stop_timers) {
    SLOG(5) << *this << " " << __func__ << ": Stopping timer for "
            << stop_timer->histogram_name() << " due to new state "
            << state_string << ".";
    if (stop_timer->Stop()) {
      metrics()->ReportMilliseconds(*stop_timer);
    }
  }
}

void Service::InitializeServiceStateTransitionMetrics() {
  auto histogram = Metrics::GetFullMetricName(
      Metrics::kMetricTimeToConfigMillisecondsSuffix, technology());
  AddServiceStateTransitionTimer(histogram, kStateConfiguring, kStateConnected);
  histogram = Metrics::GetFullMetricName(
      Metrics::kMetricTimeToPortalMillisecondsSuffix, technology());
  AddServiceStateTransitionTimer(histogram, kStateConnected,
                                 kStateNoConnectivity);
  histogram = Metrics::GetFullMetricName(
      Metrics::kMetricTimeToRedirectFoundMillisecondsSuffix, technology());
  AddServiceStateTransitionTimer(histogram, kStateConnected,
                                 kStateRedirectFound);
  histogram = Metrics::GetFullMetricName(
      Metrics::kMetricTimeToOnlineMillisecondsSuffix, technology());
  AddServiceStateTransitionTimer(histogram, kStateConnected, kStateOnline);
}

void Service::AddServiceStateTransitionTimer(const std::string& histogram_name,
                                             Service::ConnectState start_state,
                                             Service::ConnectState stop_state) {
  SLOG(5) << *this << " " << __func__ << ": Adding " << histogram_name
          << " for " << ConnectStateToString(start_state) << " -> "
          << ConnectStateToString(stop_state);
  CHECK(start_state < stop_state);
  int num_buckets = Metrics::kTimerHistogramNumBuckets;
  int max_ms = Metrics::kTimerHistogramMillisecondsMax;
  if (base::EndsWith(histogram_name,
                     Metrics::kMetricTimeToJoinMillisecondsSuffix,
                     base::CompareCase::SENSITIVE)) {
    // TimeToJoin state transition has a timeout of 70s in wpa_supplicant (see
    // b/265183655 for more details). Use a larger number of buckets and max
    // value to capture this.
    num_buckets = Metrics::kTimerHistogramNumBucketsLarge;
    max_ms = Metrics::kTimerHistogramMillisecondsMaxLarge;
  }
  auto timer = std::make_unique<chromeos_metrics::TimerReporter>(
      histogram_name, Metrics::kTimerHistogramMillisecondsMin, max_ms,
      num_buckets);
  service_metrics_->start_on_state[start_state].push_back(timer.get());
  service_metrics_->stop_on_state[stop_state].push_back(timer.get());
  service_metrics_->timers.push_back(std::move(timer));
}

void Service::UpdateNetworkValidationMode() {
  if (!attached_network_) {
    return;
  }
  const NetworkMonitor::ValidationMode validation_mode =
      GetNetworkValidationMode();
  if (validation_mode == NetworkMonitor::ValidationMode::kDisabled) {
    // If network validation is disabled for this technology, immediately set
    // the service state to "Online".
    LOG(INFO) << *this << " " << __func__
              << ": Network validation is disabled for this Service";
    SetState(Service::kStateOnline);
  }
  attached_network_->UpdateNetworkValidationMode(validation_mode);
}

void Service::NetworkEventHandler::OnNetworkValidationStart(int interface_index,
                                                            bool is_failure) {
  if (service_->IsConnected() && is_failure) {
    service_->SetState(Service::kStateNoConnectivity);
  }
}

void Service::NetworkEventHandler::OnNetworkValidationStop(int interface_index,
                                                           bool is_failure) {
  if (!service_->IsConnected()) {
    return;
  }
  if (is_failure) {
    service_->SetState(Service::kStateNoConnectivity);
  } else {
    service_->SetState(Service::kStateOnline);
  }
}

void Service::NetworkEventHandler::OnNetworkValidationResult(
    int interface_index, const NetworkMonitor::Result& result) {
  if (!service_->IsConnected()) {
    // A race can happen if the Service is currently disconnecting.
    LOG(WARNING) << *service_ << " " << __func__
                 << ": Portal detection completed but service is not connected";
    return;
  }

  // Set the probe URL from PortalDetector or sign-in URL from CAPPORT query if
  // the network validation found it, otherwise clear it.
  if (result.target_url) {
    service_->SetProbeUrl(result.target_url->ToString());
  } else {
    service_->SetProbeUrl("");
  }

  switch (result.validation_state) {
    case PortalDetector::ValidationState::kInternetConnectivity:
      service_->SetState(Service::kStateOnline);
      break;
    case PortalDetector::ValidationState::kPortalRedirect:
    case PortalDetector::ValidationState::kPortalSuspected:
      service_->SetState(Service::kStateRedirectFound);
      break;
    case PortalDetector::ValidationState::kNoConnectivity:
      service_->SetState(Service::kStateNoConnectivity);
      break;
  }
}

void Service::NetworkEventHandler::OnIPConfigsPropertyUpdated(
    int interface_index) {
  service_->EmitNetworkConfigPropertyChange();
  service_->UpdateEnableRFC8925();
}

void Service::EmitNetworkConfigPropertyChange() {
  adaptor_->EmitKeyValueStoreChanged(kNetworkConfigProperty,
                                     GetNetworkConfigDict(nullptr));
}

void Service::UpdateEnableRFC8925() {
  if (!attached_network_) {
    return;
  }

  const auto& network_config = attached_network_->GetNetworkConfig();

  const net_base::IPv6CIDR kIPv6LockLocalCIDR =
      net_base::IPv6CIDR::CreateFromStringAndPrefix("fe80::", 10).value();
  bool has_ipv6_link_local = false;
  bool has_ipv6_non_link_local = false;
  for (const auto& addr : network_config.dns_servers) {
    // Ignore IPv4 DNS servers. It won't provide any information to make this
    // decision.
    const auto ipv6_addr = addr.ToIPv6Address();
    if (!ipv6_addr) {
      continue;
    }
    if (kIPv6LockLocalCIDR.InSameSubnetWith(ipv6_addr.value())) {
      has_ipv6_link_local = true;
    } else {
      has_ipv6_non_link_local = true;
    }
  }

  // The basic assumption here is that when the network is losing DNS servers
  // from RNDSS, they should be removed together, otherwise there is a corner
  // case that non-link-local address is removed at first and a link-local one
  // is left. DNS servers in StaticConfig maybe removed one-by-one, but it's not
  // expected that user will edit the list in each connection session, so this
  // shouldn't be a problem.
  if (has_ipv6_non_link_local) {
    enable_rfc_8925_ = true;
  } else if (has_ipv6_link_local) {
    enable_rfc_8925_ = false;
  }
  // For other cases, there is either a) no DNS server or b) only IPv4 DNS
  // server. In either case, we don't have enough information to change the flag
  // value.
}

void Service::GetExtraTrafficCounters(
    Network::GetTrafficCountersCallback callback) {
  std::move(callback).Run({});
}

std::string Service::LoggingTag() const {
  if (attached_network_) {
    return attached_network_->LoggingTag();
  }
  // If the Service has no Network attached, then there is no Device currently
  // selecting this Service.
  return base::StrCat({"unselected ", log_name(), " sid=none"});
}

std::ostream& operator<<(std::ostream& stream, const Service& service) {
  return stream << service.LoggingTag();
}

}  // namespace shill
