// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_capability_3gpp.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/check.h>
#include <base/check_op.h>
#include <base/files/file.h>
#include <base/notreached.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>
#include <ModemManager/ModemManager.h>

#include <string>
#include <vector>

#include "shill/adaptor_interfaces.h"
#include "shill/cellular/cellular_bearer.h"
#include "shill/cellular/cellular_pco.h"
#include "shill/cellular/cellular_service.h"
#include "shill/cellular/mobile_operator_info.h"
#include "shill/cellular/modem_info.h"
#include "shill/cellular/pending_activation_store.h"
#include "shill/cellular/verizon_subscription_state.h"
#include "shill/control_interface.h"
#include "shill/dbus/dbus_properties_proxy.h"
#include "shill/device_id.h"
#include "shill/error.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/property_accessor.h"

using base::Bind;
using base::Closure;
using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCellular;
static string ObjectID(const CellularCapability3gpp* c) {
  return c->cellular()->GetRpcIdentifier().value();
}
}  // namespace Logging

// static
const char CellularCapability3gpp::kConnectApn[] = "apn";
const char CellularCapability3gpp::kConnectUser[] = "user";
const char CellularCapability3gpp::kConnectPassword[] = "password";
const char CellularCapability3gpp::kConnectAllowedAuth[] = "allowed-auth";
const char CellularCapability3gpp::kConnectAllowRoaming[] = "allow-roaming";
const char CellularCapability3gpp::kConnectIpType[] = "ip-type";
const int64_t CellularCapability3gpp::kEnterPinTimeoutMilliseconds = 20000;
const int64_t
    CellularCapability3gpp::kRegistrationDroppedUpdateTimeoutMilliseconds =
        15000;
const RpcIdentifier CellularCapability3gpp::kRootPath = RpcIdentifier("/");
const char CellularCapability3gpp::kStatusProperty[] = "status";
const char CellularCapability3gpp::kOperatorLongProperty[] = "operator-long";
const char CellularCapability3gpp::kOperatorShortProperty[] = "operator-short";
const char CellularCapability3gpp::kOperatorCodeProperty[] = "operator-code";
const char CellularCapability3gpp::kOperatorAccessTechnologyProperty[] =
    "access-technology";

const char CellularCapability3gpp::kRsrpProperty[] = "rsrp";
const char CellularCapability3gpp::kRssiProperty[] = "rssi";
// Range of RSSI's reported to UI. Any RSSI out of this range is clamped to the
// nearest threshold.
const CellularCapability3gpp::SignalQualityBounds
    CellularCapability3gpp::kRssiBounds = {-105, -83};
// Range of RSRP's reported to UI. Any RSRP out of this range is clamped to the
// nearest threshold.
const CellularCapability3gpp::SignalQualityBounds
    CellularCapability3gpp::kRsrpBounds = {-128, -88};

const int CellularCapability3gpp::kSetPowerStateTimeoutMilliseconds = 20000;
const int CellularCapability3gpp::kUnknownLockRetriesLeft = 999;

namespace {

const int kSignalQualityUpdateRateSeconds = 30;

// Plugin strings via ModemManager.
const char kTelitMMPlugin[] = "Telit";

// This identifier is specified in the serviceproviders.prototxt file.
const char kVzwIdentifier[] = "c83d6597-dc91-4d48-a3a7-d86b80123751";
const size_t kVzwMdnLength = 10;

// Keys for the entries of Profiles.
const char kProfileApn[] = "apn";
const char kProfileUsername[] = "username";
const char kProfilePassword[] = "password";
const char kProfileAuthType[] = "auth-type";

string AccessTechnologyToString(uint32_t access_technologies) {
  // Order is important. Return the highest radio access technology.
  if (access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_LTE)
    return kNetworkTechnologyLte;
  if (access_technologies &
      (MM_MODEM_ACCESS_TECHNOLOGY_EVDO0 | MM_MODEM_ACCESS_TECHNOLOGY_EVDOA |
       MM_MODEM_ACCESS_TECHNOLOGY_EVDOB))
    return kNetworkTechnologyEvdo;
  if (access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_1XRTT)
    return kNetworkTechnology1Xrtt;
  if (access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS)
    return kNetworkTechnologyHspaPlus;
  if (access_technologies &
      (MM_MODEM_ACCESS_TECHNOLOGY_HSPA | MM_MODEM_ACCESS_TECHNOLOGY_HSUPA |
       MM_MODEM_ACCESS_TECHNOLOGY_HSDPA))
    return kNetworkTechnologyHspa;
  if (access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_UMTS)
    return kNetworkTechnologyUmts;
  if (access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_EDGE)
    return kNetworkTechnologyEdge;
  if (access_technologies & MM_MODEM_ACCESS_TECHNOLOGY_GPRS)
    return kNetworkTechnologyGprs;
  if (access_technologies &
      (MM_MODEM_ACCESS_TECHNOLOGY_GSM_COMPACT | MM_MODEM_ACCESS_TECHNOLOGY_GSM))
    return kNetworkTechnologyGsm;
  return "";
}

string AccessTechnologyToTechnologyFamily(uint32_t access_technologies) {
  if (access_technologies &
      (MM_MODEM_ACCESS_TECHNOLOGY_LTE | MM_MODEM_ACCESS_TECHNOLOGY_HSPA_PLUS |
       MM_MODEM_ACCESS_TECHNOLOGY_HSPA | MM_MODEM_ACCESS_TECHNOLOGY_HSUPA |
       MM_MODEM_ACCESS_TECHNOLOGY_HSDPA | MM_MODEM_ACCESS_TECHNOLOGY_UMTS |
       MM_MODEM_ACCESS_TECHNOLOGY_EDGE | MM_MODEM_ACCESS_TECHNOLOGY_GPRS |
       MM_MODEM_ACCESS_TECHNOLOGY_GSM_COMPACT | MM_MODEM_ACCESS_TECHNOLOGY_GSM))
    return kTechnologyFamilyGsm;
  if (access_technologies &
      (MM_MODEM_ACCESS_TECHNOLOGY_EVDO0 | MM_MODEM_ACCESS_TECHNOLOGY_EVDOA |
       MM_MODEM_ACCESS_TECHNOLOGY_EVDOB | MM_MODEM_ACCESS_TECHNOLOGY_1XRTT))
    return kTechnologyFamilyCdma;
  return "";
}

MMBearerAllowedAuth ApnAuthenticationToMMBearerAllowedAuth(
    const std::string& authentication) {
  if (authentication == kApnAuthenticationPap) {
    return MM_BEARER_ALLOWED_AUTH_PAP;
  }
  if (authentication == kApnAuthenticationChap) {
    return MM_BEARER_ALLOWED_AUTH_CHAP;
  }
  return MM_BEARER_ALLOWED_AUTH_UNKNOWN;
}

MMBearerIpFamily IpTypeToMMBearerIpFamily(const std::string& ip_type) {
  if (ip_type == kApnIpTypeV6) {
    return MM_BEARER_IP_FAMILY_IPV6;
  }
  if (ip_type == kApnIpTypeV4V6) {
    return MM_BEARER_IP_FAMILY_IPV4V6;
  }

  // A cellular device is disabled before the system goes into suspend mode.
  // However, outstanding TCP sockets may not be nuked when the associated
  // network interface goes down. When the system resumes from suspend, the
  // cellular device is re-enabled and may reconnect to the network, which
  // acquire a new IPv6 address on the network interface. However, those
  // outstanding TCP sockets may initiate traffic with the old IPv6 address.
  // Some networks may not like the fact that two IPv6 addresses originated
  // from the same modem within a connection session and may drop the
  // connection. So make IPv4-only the default to work around the issue while
  // we verify IPv6 support on different carriers.
  return MM_BEARER_IP_FAMILY_IPV4;
}

std::string MMBearerAllowedAuthToApnAuthentication(
    MMBearerAllowedAuth authentication) {
  switch (authentication) {
    case MM_BEARER_ALLOWED_AUTH_PAP:
      return kApnAuthenticationPap;
    case MM_BEARER_ALLOWED_AUTH_CHAP:
      return kApnAuthenticationChap;
    default:
      return "";
  }
}

bool IsRegisteredState(MMModem3gppRegistrationState state) {
  return (state == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
          state == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING);
}

std::string RegistrationStateToString(MMModem3gppRegistrationState state) {
  switch (state) {
    case MM_MODEM_3GPP_REGISTRATION_STATE_IDLE:
      return "Idle";
    case MM_MODEM_3GPP_REGISTRATION_STATE_HOME:
      return "Home";
    case MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING:
      return "Searching";
    case MM_MODEM_3GPP_REGISTRATION_STATE_DENIED:
      return "Denied";
    case MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN:
      return "Unknown";
    case MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING:
      return "Roaming";
    case MM_MODEM_3GPP_REGISTRATION_STATE_HOME_SMS_ONLY:
      return "HomeSmsOnly";
    case MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING_SMS_ONLY:
      return "RoamingSmsOnly";
    case MM_MODEM_3GPP_REGISTRATION_STATE_EMERGENCY_ONLY:
      return "EmergencyOnly";
    case MM_MODEM_3GPP_REGISTRATION_STATE_HOME_CSFB_NOT_PREFERRED:
      return "HomeCsfbNotPreferred";
    case MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING_CSFB_NOT_PREFERRED:
      return "RoamingCsfbNotPreferred";
    case MM_MODEM_3GPP_REGISTRATION_STATE_ATTACHED_RLOS:
      return "AttachedRlos";
  }
}

}  // namespace

CellularCapability3gpp::CellularCapability3gpp(Cellular* cellular,
                                               ModemInfo* modem_info)
    : CellularCapability(cellular, modem_info),
      metrics_(modem_info->manager()->metrics()),
      mobile_operator_info_(
          new MobileOperatorInfo(cellular->dispatcher(), "ParseScanResult")),
      registration_state_(MM_MODEM_3GPP_REGISTRATION_STATE_UNKNOWN),
      current_capabilities_(MM_MODEM_CAPABILITY_NONE),
      access_technologies_(MM_MODEM_ACCESS_TECHNOLOGY_UNKNOWN),
      resetting_(false),
      subscription_state_(SubscriptionState::kUnknown),
      reset_done_(false),
      registration_dropped_update_timeout_milliseconds_(
          kRegistrationDroppedUpdateTimeoutMilliseconds),
      weak_ptr_factory_(this) {
  SLOG(this, 2) << "Cellular capability constructed: 3GPP";
  mobile_operator_info_->Init();
}

CellularCapability3gpp::~CellularCapability3gpp() {
  SLOG(this, 2) << "Cellular capability destroyed: 3GPP";
}

KeyValueStore CellularCapability3gpp::SimLockStatusToProperty(
    Error* /*error*/) {
  KeyValueStore status;
  string lock_type;
  switch (sim_lock_status_.lock_type) {
    case MM_MODEM_LOCK_SIM_PIN:
      lock_type = "sim-pin";
      break;
    case MM_MODEM_LOCK_SIM_PUK:
      lock_type = "sim-puk";
      break;
    default:
      lock_type = "";
      break;
  }
  status.Set<bool>(kSIMLockEnabledProperty, sim_lock_status_.enabled);
  status.Set<string>(kSIMLockTypeProperty, lock_type);
  status.Set<int32_t>(kSIMLockRetriesLeftProperty,
                      sim_lock_status_.retries_left);
  return status;
}

bool CellularCapability3gpp::SetPrimarySimSlotForIccid(
    const std::string& iccid) {
  SLOG(this, 2) << __func__ << ": " << iccid;
  for (const auto& iter : sim_properties_) {
    const SimProperties* properties = &iter.second;
    if (properties->iccid == iccid) {
      SetPrimarySimSlot(properties->slot);
      return true;
    }
  }
  return false;
}

void CellularCapability3gpp::InitProxies() {
  if (proxies_initialized_)
    return;
  SLOG(this, 3) << __func__;
  proxies_initialized_ = true;
  modem_3gpp_proxy_ = control_interface()->CreateMM1ModemModem3gppProxy(
      cellular()->dbus_path(), cellular()->dbus_service());
  modem_proxy_ = control_interface()->CreateMM1ModemProxy(
      cellular()->dbus_path(), cellular()->dbus_service());
  modem_signal_proxy_ = control_interface()->CreateMM1ModemSignalProxy(
      cellular()->dbus_path(), cellular()->dbus_service());
  modem_simple_proxy_ = control_interface()->CreateMM1ModemSimpleProxy(
      cellular()->dbus_path(), cellular()->dbus_service());

  modem_location_proxy_ = control_interface()->CreateMM1ModemLocationProxy(
      cellular()->dbus_path(), cellular()->dbus_service());
  dbus_properties_proxy_ = control_interface()->CreateDBusPropertiesProxy(
      cellular()->dbus_path(), cellular()->dbus_service());

  modem_proxy_->set_state_changed_callback(
      Bind(&CellularCapability3gpp::OnModemStateChangedSignal,
           weak_ptr_factory_.GetWeakPtr()));

  // |sim_proxy_| is created when |sim_path_| is known.
}

void CellularCapability3gpp::StartModem(Error* error,
                                        const ResultCallback& callback) {
  SLOG(this, 1) << __func__;
  InitProxies();
  CHECK(!callback.is_null());
  Error local_error(Error::kOperationInitiated);
  metrics_->NotifyDeviceEnableStarted(cellular()->interface_index());
  modem_proxy_->Enable(true, &local_error,
                       Bind(&CellularCapability3gpp::EnableModemCompleted,
                            weak_ptr_factory_.GetWeakPtr(), callback),
                       kTimeoutEnable);
  if (local_error.IsFailure()) {
    SLOG(this, 2) << __func__ << "Call to modem_proxy_->Enable() failed";
  }
  if (error) {
    error->CopyFrom(local_error);
  }
}

void CellularCapability3gpp::EnableModemCompleted(
    const ResultCallback& callback, const Error& error) {
  SLOG(this, 1) << __func__ << " error=" << error;

  if (error.IsFailure()) {
    callback.Run(error);
    return;
  }

  if (IsLocationUpdateSupported()) {
    SetupLocation(MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI,
                  /*signal_location=*/false,
                  Bind(&CellularCapability3gpp::OnSetupLocationReply,
                       weak_ptr_factory_.GetWeakPtr()));
  }

  ResultCallback setup_signal_callback =
      Bind(&CellularCapability3gpp::OnSetupSignalReply,
           weak_ptr_factory_.GetWeakPtr());
  SetupSignal(kSignalQualityUpdateRateSeconds, setup_signal_callback);

  // After modem is enabled, it should be possible to get properties
  // TODO(jglasgow): handle errors from GetProperties
  GetProperties();
  callback.Run(error);
}

void CellularCapability3gpp::StopModem(Error* error,
                                       const ResultCallback& callback) {
  CHECK(!callback.is_null());
  CHECK(error);
  // If there is an outstanding registration change, simply ignore it since
  // the service will be destroyed anyway.
  if (!registration_dropped_update_callback_.IsCancelled()) {
    registration_dropped_update_callback_.Cancel();
    SLOG(this, 2) << __func__ << " Cancelled delayed deregister.";
  }

  cellular()->dispatcher()->PostTask(
      FROM_HERE, Bind(&CellularCapability3gpp::Stop_Disable,
                      weak_ptr_factory_.GetWeakPtr(), callback));
}

void CellularCapability3gpp::Stop_Disable(const ResultCallback& callback) {
  SLOG(this, 3) << __func__;
  Error error;
  metrics_->NotifyDeviceDisableStarted(cellular()->interface_index());
  modem_proxy_->Enable(false, &error,
                       Bind(&CellularCapability3gpp::Stop_DisableCompleted,
                            weak_ptr_factory_.GetWeakPtr(), callback),
                       kTimeoutEnable);
  if (error.IsFailure())
    callback.Run(error);
}

void CellularCapability3gpp::Stop_DisableCompleted(
    const ResultCallback& callback, const Error& error) {
  SLOG(this, 3) << __func__;

  if (error.IsSuccess()) {
    // The modem has been successfully disabled, but we still need to power it
    // down.
    Stop_PowerDown(callback);
  } else {
    // An error occurred; terminate the disable sequence.
    callback.Run(error);
  }
}

void CellularCapability3gpp::Stop_PowerDown(const ResultCallback& callback) {
  SLOG(this, 3) << __func__;
  Error error;
  modem_proxy_->SetPowerState(
      MM_MODEM_POWER_STATE_LOW, &error,
      Bind(&CellularCapability3gpp::Stop_PowerDownCompleted,
           weak_ptr_factory_.GetWeakPtr(), callback),
      kSetPowerStateTimeoutMilliseconds);

  if (error.IsFailure())
    // This really shouldn't happen, but if it does, report success,
    // because a stop initiated power down is only called if the
    // modem was successfully disabled, but the failure of this
    // operation should still be propagated up as a successful disable.
    Stop_PowerDownCompleted(callback, error);
}

// Note: if we were in the middle of powering down the modem when the
// system suspended, we might not get this event from
// ModemManager. And we might not even get a timeout from dbus-c++,
// because StartModem re-initializes proxies.
void CellularCapability3gpp::Stop_PowerDownCompleted(
    const ResultCallback& callback, const Error& error) {
  SLOG(this, 3) << __func__;

  if (error.IsFailure())
    SLOG(this, 2) << "Ignoring error returned by SetPowerState: " << error;

  // Since the disable succeeded, if power down fails, we currently fail
  // silently, i.e. we need to report the disable operation as having
  // succeeded.
  metrics_->NotifyDeviceDisableFinished(cellular()->interface_index());
  ReleaseProxies();
  callback.Run(Error());
}

void CellularCapability3gpp::Connect(const KeyValueStore& properties,
                                     Error* error,
                                     const ResultCallback& callback) {
  SLOG(this, 3) << __func__;
  RpcIdentifierCallback cb = Bind(&CellularCapability3gpp::OnConnectReply,
                                  weak_ptr_factory_.GetWeakPtr(), callback);
  modem_simple_proxy_->Connect(properties, error, cb, kTimeoutConnect);
}

void CellularCapability3gpp::Disconnect(Error* error,
                                        const ResultCallback& callback) {
  SLOG(this, 3) << __func__;
  if (modem_simple_proxy_) {
    SLOG(this, 2) << "Disconnect all bearers.";
    // If "/" is passed as the bearer path, ModemManager will disconnect all
    // bearers.
    modem_simple_proxy_->Disconnect(kRootPath, error, callback,
                                    kTimeoutDisconnect);
  }
}

void CellularCapability3gpp::CompleteActivation(Error* error) {
  SLOG(this, 3) << __func__;

  // Persist the ICCID as "Pending Activation".
  // We're assuming that when this function gets called,
  // |cellular()->iccid()| will be non-empty. We still check here that
  // is non-empty, though something is wrong if it is empty.
  const string& iccid = cellular()->iccid();
  if (iccid.empty()) {
    SLOG(this, 2) << "SIM identifier not available. Nothing to do.";
    return;
  }

  modem_info()->pending_activation_store()->SetActivationState(
      PendingActivationStore::kIdentifierICCID, iccid,
      PendingActivationStore::kStatePending);
  UpdatePendingActivationState();

  SLOG(this, 2) << "Resetting modem for activation.";
  ResetAfterActivation();
}

void CellularCapability3gpp::ResetAfterActivation() {
  SLOG(this, 3) << __func__;

  // Here the initial call to Reset might fail in rare cases. Simply ignore.
  Error error;
  ResultCallback callback =
      Bind(&CellularCapability3gpp::OnResetAfterActivationReply,
           weak_ptr_factory_.GetWeakPtr());
  Reset(&error, callback);
  if (error.IsFailure())
    SLOG(this, 2) << "Failed to reset after activation.";
}

void CellularCapability3gpp::OnResetAfterActivationReply(const Error& error) {
  SLOG(this, 3) << __func__;
  if (error.IsFailure()) {
    SLOG(this, 2) << "Failed to reset after activation. Try again later.";
    // TODO(armansito): Maybe post a delayed reset task?
    return;
  }
  reset_done_ = true;
  UpdatePendingActivationState();
}

void CellularCapability3gpp::UpdatePendingActivationState() {
  SLOG(this, 3) << __func__;

  const string& iccid = cellular()->iccid();
  bool registered =
      registration_state_ == MM_MODEM_3GPP_REGISTRATION_STATE_HOME;

  // We know a service is activated if |subscription_state_| is
  // SubscriptionState::kProvisioned / SubscriptionState::kOutOfCredits
  // In the case that |subscription_state_| is SubscriptionState::kUnknown, we
  // fallback on checking for a valid MDN.
  bool activated =
      ((subscription_state_ == SubscriptionState::kProvisioned) ||
       (subscription_state_ == SubscriptionState::kOutOfCredits)) ||
      ((subscription_state_ == SubscriptionState::kUnknown) && IsMdnValid());

  if (activated && !iccid.empty())
    modem_info()->pending_activation_store()->RemoveEntry(
        PendingActivationStore::kIdentifierICCID, iccid);

  CellularServiceRefPtr service = cellular()->service();

  if (!service)
    return;

  if (service->activation_state() == kActivationStateActivated)
    // Either no service or already activated. Nothing to do.
    return;

  // If the ICCID is not available, the following logic can be delayed until it
  // becomes available.
  if (iccid.empty())
    return;

  PendingActivationStore::State state =
      modem_info()->pending_activation_store()->GetActivationState(
          PendingActivationStore::kIdentifierICCID, iccid);
  switch (state) {
    case PendingActivationStore::kStatePending:
      // Always mark the service as activating here, as the ICCID could have
      // been unavailable earlier.
      service->SetActivationState(kActivationStateActivating);
      if (reset_done_) {
        SLOG(this, 2) << "Post-payment activation reset complete.";
        modem_info()->pending_activation_store()->SetActivationState(
            PendingActivationStore::kIdentifierICCID, iccid,
            PendingActivationStore::kStateActivated);
      }
      break;
    case PendingActivationStore::kStateActivated:
      if (registered) {
        // Trigger auto connect here.
        SLOG(this, 2) << "Modem has been reset at least once, try to "
                      << "autoconnect to force MDN to update.";
        service->AutoConnect();
      }
      break;
    case PendingActivationStore::kStateUnknown:
      // No entry exists for this ICCID. Nothing to do.
      break;
    default:
      NOTREACHED();
  }
}

string CellularCapability3gpp::GetMdnForOLP(
    const MobileOperatorInfo* operator_info) const {
  // TODO(benchan): This is ugly. Remove carrier specific code once we move
  // mobile activation logic to carrier-specific extensions (crbug.com/260073).
  const string& mdn = cellular()->mdn();
  if (!operator_info->IsMobileNetworkOperatorKnown()) {
    // Can't make any carrier specific modifications.
    return mdn;
  }

  if (operator_info->uuid() == kVzwIdentifier) {
    // subscription_state_ is the definitive indicator of whether we need
    // activation. The OLP expects an all zero MDN in that case.
    if (subscription_state_ == SubscriptionState::kUnprovisioned ||
        mdn.empty()) {
      return string(kVzwMdnLength, '0');
    }
    if (mdn.length() > kVzwMdnLength) {
      return mdn.substr(mdn.length() - kVzwMdnLength);
    }
  }
  return mdn;
}

void CellularCapability3gpp::ReleaseProxies() {
  if (!proxies_initialized_)
    return;
  SLOG(this, 3) << __func__;
  proxies_initialized_ = false;
  modem_3gpp_proxy_.reset();
  modem_proxy_.reset();
  modem_location_proxy_.reset();
  modem_signal_proxy_.reset();
  modem_simple_proxy_.reset();
  dbus_properties_proxy_.reset();

  // |sim_proxy_| is managed through OnAllSimPropertiesReceived() and thus
  // shouldn't be cleared here in order to keep it in sync with |sim_path_|.
}

void CellularCapability3gpp::UpdateServiceActivationState() {
  CellularServiceRefPtr service = cellular()->service();
  if (!service)
    return;

  service->NotifySubscriptionStateChanged(subscription_state_);

  const string& iccid = cellular()->iccid();
  string activation_state;
  PendingActivationStore::State state =
      modem_info()->pending_activation_store()->GetActivationState(
          PendingActivationStore::kIdentifierICCID, iccid);
  if ((subscription_state_ == SubscriptionState::kUnknown ||
       subscription_state_ == SubscriptionState::kUnprovisioned) &&
      !iccid.empty() && state == PendingActivationStore::kStatePending) {
    activation_state = kActivationStateActivating;
  } else if (IsServiceActivationRequired()) {
    activation_state = kActivationStateNotActivated;
  } else {
    activation_state = kActivationStateActivated;
  }
  service->SetActivationState(activation_state);
}

void CellularCapability3gpp::OnServiceCreated() {
  // ModemManager might have issued some property updates before the service
  // object was created to receive the updates, so we explicitly refresh the
  // properties here.
  GetProperties();

  // GetProperties() could trigger a call to Handle3gppRegistrationChange which
  // could destroy the service.
  if (!cellular()->service())
    return;

  cellular()->service()->SetActivationType(CellularService::kActivationTypeOTA);
  UpdateServiceActivationState();

  // Make sure that the network technology is set when the service gets
  // created, just in case.
  cellular()->service()->SetNetworkTechnology(GetNetworkTechnologyString());
}

void CellularCapability3gpp::SetupConnectProperties(KeyValueStore* properties) {
  apn_try_list_ = cellular()->BuildApnTryList();
  FillConnectPropertyMap(properties);
}

void CellularCapability3gpp::FillConnectPropertyMap(KeyValueStore* properties) {
  properties->Set<bool>(kConnectAllowRoaming,
                        cellular()->IsRoamingAllowedOrRequired());

  if (apn_try_list_.empty())
    return;

  // Leave the APN at the front of the list, so that it can be recorded
  // if the connect attempt succeeds.
  Stringmap apn_info = apn_try_list_.front();
  SLOG(this, 2) << __func__ << ": Using APN " << apn_info[kApnProperty];
  properties->Set<string>(kConnectApn, apn_info[kApnProperty]);
  if (base::Contains(apn_info, kApnUsernameProperty))
    properties->Set<string>(kConnectUser, apn_info[kApnUsernameProperty]);
  if (base::Contains(apn_info, kApnPasswordProperty))
    properties->Set<string>(kConnectPassword, apn_info[kApnPasswordProperty]);
  if (base::Contains(apn_info, kApnAuthenticationProperty)) {
    MMBearerAllowedAuth allowed_auth = ApnAuthenticationToMMBearerAllowedAuth(
        apn_info[kApnAuthenticationProperty]);
    if (allowed_auth != MM_BEARER_ALLOWED_AUTH_UNKNOWN)
      properties->Set<uint32_t>(kConnectAllowedAuth, allowed_auth);
  }
  if (base::Contains(apn_info, kApnIpTypeProperty)) {
    properties->Set<uint32_t>(
        kConnectIpType, IpTypeToMMBearerIpFamily(apn_info[kApnIpTypeProperty]));
  }
}

void CellularCapability3gpp::OnConnectReply(const ResultCallback& callback,
                                            const RpcIdentifier& bearer,
                                            const Error& error) {
  SLOG(this, 3) << __func__ << "(" << error << ")";

  CellularServiceRefPtr service = cellular()->service();
  if (!service) {
    // The service could have been deleted before our Connect() request
    // completes if the modem was enabled and then quickly disabled.
    apn_try_list_.clear();
  } else if (error.IsFailure()) {
    service->ClearLastGoodApn();
    // The APN that was just tried (and failed) is still at the
    // front of the list, about to be removed. If the list is empty
    // after that, try one last time without an APN. This may succeed
    // with some modems in some cases.
    if (RetriableConnectError(error) && !apn_try_list_.empty()) {
      apn_try_list_.pop_front();
      SLOG(this, 2) << "Connect failed with invalid APN, "
                    << apn_try_list_.size() << " remaining APNs to try";
      KeyValueStore props;
      FillConnectPropertyMap(&props);
      Error error;
      Connect(props, &error, callback);
      return;
    }
  } else {
    if (!apn_try_list_.empty()) {
      service->SetLastGoodApn(apn_try_list_.front());
      apn_try_list_.clear();
    }
    SLOG(this, 2) << "Connected bearer " << bearer.value();
  }

  if (!callback.is_null())
    callback.Run(error);

  UpdatePendingActivationState();
}

void CellularCapability3gpp::FillInitialEpsBearerPropertyMap(
    KeyValueStore* properties) {
  std::deque<Stringmap> apn_list = cellular()->BuildApnTryList();
  auto apn_info = apn_list.end();

  // keep only 'attach APN'
  for (apn_info = apn_list.begin(); apn_info != apn_list.end(); apn_info++) {
    if (base::Contains(*apn_info, kApnAttachProperty))
      break;
  }

  if (apn_info == apn_list.end()) {
    SLOG(this, 2) << __func__ << ": no Attach APN.";
    return;
  }

  SLOG(this, 2) << __func__ << ": Using APN " << (*apn_info)[kApnProperty];
  properties->Set<string>(kConnectApn, (*apn_info)[kApnProperty]);
  if (base::Contains(*apn_info, kApnUsernameProperty))
    properties->Set<string>(kConnectUser, (*apn_info)[kApnUsernameProperty]);
  if (base::Contains(*apn_info, kApnPasswordProperty)) {
    properties->Set<string>(kConnectPassword,
                            (*apn_info)[kApnPasswordProperty]);
  }
  if (base::Contains(*apn_info, kApnAuthenticationProperty)) {
    MMBearerAllowedAuth allowed_auth = ApnAuthenticationToMMBearerAllowedAuth(
        (*apn_info)[kApnAuthenticationProperty]);
    if (allowed_auth != MM_BEARER_ALLOWED_AUTH_UNKNOWN)
      properties->Set<uint32_t>(kConnectAllowedAuth, allowed_auth);
  }
}

void CellularCapability3gpp::GetProperties() {
  SLOG(this, 3) << __func__;
  CHECK(dbus_properties_proxy_);

  auto properties = dbus_properties_proxy_->GetAll(MM_DBUS_INTERFACE_MODEM);
  OnModemPropertiesChanged(properties);

  auto properties_3gpp =
      dbus_properties_proxy_->GetAll(MM_DBUS_INTERFACE_MODEM_MODEM3GPP);
  OnModem3gppPropertiesChanged(properties_3gpp);

  auto properties_signal =
      dbus_properties_proxy_->GetAll(MM_DBUS_INTERFACE_MODEM_SIGNAL);
  OnModemSignalPropertiesChanged(properties_signal);
}

void CellularCapability3gpp::UpdateServiceOLP() {
  SLOG(this, 3) << __func__;

  // OLP is based off of the Home Provider.
  if (!cellular()->home_provider_info()->IsMobileNetworkOperatorKnown()) {
    return;
  }

  const vector<MobileOperatorInfo::OnlinePortal>& olp_list =
      cellular()->home_provider_info()->olp_list();
  if (olp_list.empty()) {
    return;
  }

  if (olp_list.size() > 1) {
    SLOG(this, 1) << "Found multiple online portals. Choosing the first.";
  }
  string post_data = olp_list[0].post_data;
  base::ReplaceSubstringsAfterOffset(&post_data, 0, "${iccid}",
                                     cellular()->iccid());
  base::ReplaceSubstringsAfterOffset(&post_data, 0, "${imei}",
                                     cellular()->imei());
  base::ReplaceSubstringsAfterOffset(&post_data, 0, "${imsi}",
                                     cellular()->imsi());
  base::ReplaceSubstringsAfterOffset(
      &post_data, 0, "${mdn}", GetMdnForOLP(cellular()->home_provider_info()));
  base::ReplaceSubstringsAfterOffset(&post_data, 0, "${min}",
                                     cellular()->min());
  cellular()->service()->SetOLP(olp_list[0].url, olp_list[0].method, post_data);
}

void CellularCapability3gpp::UpdateActiveBearer() {
  SLOG(this, 3) << __func__;

  // Look for the first active bearer and use its path as the connected
  // one. Right now, we don't allow more than one active bearer.
  active_bearer_.reset();
  for (const auto& path : bearer_paths_) {
    auto bearer = std::make_unique<CellularBearer>(control_interface(), path,
                                                   cellular()->dbus_service());
    // The bearer object may have vanished before ModemManager updates the
    // 'Bearers' property.
    if (!bearer->Init())
      continue;

    if (!bearer->connected())
      continue;

    SLOG(this, 2) << "Found active bearer \"" << path.value() << "\".";
    CHECK(!active_bearer_) << "Found more than one active bearer.";
    active_bearer_ = std::move(bearer);
  }

  if (!active_bearer_)
    SLOG(this, 2) << "No active bearer found.";
}

bool CellularCapability3gpp::IsServiceActivationRequired() const {
  const string& iccid = cellular()->iccid();
  // subscription_state_ is the definitive answer. If that does not work,
  // fallback on MDN based logic.
  if (subscription_state_ == SubscriptionState::kProvisioned ||
      subscription_state_ == SubscriptionState::kOutOfCredits)
    return false;

  // We are in the process of activating, ignore all other clues from the
  // network and use our own knowledge about the activation state.
  if (!iccid.empty() &&
      modem_info()->pending_activation_store()->GetActivationState(
          PendingActivationStore::kIdentifierICCID, iccid) !=
          PendingActivationStore::kStateUnknown)
    return false;

  // Network notification that the service needs to be activated.
  if (subscription_state_ == SubscriptionState::kUnprovisioned)
    return true;

  // If there is no online payment portal information, it's safer to assume
  // the service does not require activation.
  if (!cellular()->home_provider_info()->IsMobileNetworkOperatorKnown() ||
      cellular()->home_provider_info()->olp_list().empty()) {
    return false;
  }

  // If the MDN is invalid (i.e. empty or contains only zeros), the service
  // requires activation.
  return !IsMdnValid();
}

bool CellularCapability3gpp::IsActivating() const {
  return false;
}

bool CellularCapability3gpp::IsMdnValid() const {
  const string& mdn = cellular()->mdn();
  // Note that |mdn| is normalized to contain only digits in OnMdnChanged().
  for (size_t i = 0; i < mdn.size(); ++i) {
    if (mdn[i] != '0')
      return true;
  }
  return false;
}

// always called from an async context
void CellularCapability3gpp::Register(const ResultCallback& callback) {
  SLOG(this, 3) << __func__ << " \"" << cellular()->selected_network() << "\"";
  CHECK(!callback.is_null());
  Error error;
  ResultCallback cb = Bind(&CellularCapability3gpp::OnRegisterReply,
                           weak_ptr_factory_.GetWeakPtr(), callback);
  modem_3gpp_proxy_->Register(cellular()->selected_network(), &error, cb,
                              kTimeoutRegister);
  if (error.IsFailure())
    callback.Run(error);
}

void CellularCapability3gpp::RegisterOnNetwork(const string& network_id,
                                               Error* error,
                                               const ResultCallback& callback) {
  SLOG(this, 3) << __func__ << "(" << network_id << ")";
  CHECK(error);
  desired_network_ = network_id;
  ResultCallback cb = Bind(&CellularCapability3gpp::OnRegisterReply,
                           weak_ptr_factory_.GetWeakPtr(), callback);
  modem_3gpp_proxy_->Register(network_id, error, cb, kTimeoutRegister);
}

void CellularCapability3gpp::OnRegisterReply(const ResultCallback& callback,
                                             const Error& error) {
  SLOG(this, 3) << __func__ << "(" << error << ")";

  if (error.IsSuccess()) {
    cellular()->set_selected_network(desired_network_);
    desired_network_.clear();
    callback.Run(error);
    return;
  }
  // If registration on the desired network failed,
  // try to register on the home network.
  if (!desired_network_.empty()) {
    desired_network_.clear();
    cellular()->set_selected_network("");
    LOG(INFO) << "Couldn't register on selected network, trying home network";
    Register(callback);
    return;
  }
  callback.Run(error);
}

bool CellularCapability3gpp::IsRegistered() const {
  return IsRegisteredState(registration_state_);
}

void CellularCapability3gpp::SetUnregistered(bool searching) {
  // If we're already in some non-registered state, don't override that
  if (registration_state_ == MM_MODEM_3GPP_REGISTRATION_STATE_HOME ||
      registration_state_ == MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING) {
    registration_state_ =
        (searching ? MM_MODEM_3GPP_REGISTRATION_STATE_SEARCHING
                   : MM_MODEM_3GPP_REGISTRATION_STATE_IDLE);
  }
}

void CellularCapability3gpp::RequirePin(const string& pin,
                                        bool require,
                                        Error* error,
                                        const ResultCallback& callback) {
  CHECK(error);
  sim_proxy_->EnablePin(pin, require, error, callback, kTimeoutDefault);
}

void CellularCapability3gpp::EnterPin(const string& pin,
                                      Error* error,
                                      const ResultCallback& callback) {
  CHECK(error);
  SLOG(this, 3) << __func__;
  sim_proxy_->SendPin(pin, error, callback, kEnterPinTimeoutMilliseconds);
}

void CellularCapability3gpp::UnblockPin(const string& unblock_code,
                                        const string& pin,
                                        Error* error,
                                        const ResultCallback& callback) {
  CHECK(error);
  sim_proxy_->SendPuk(unblock_code, pin, error, callback, kTimeoutDefault);
}

void CellularCapability3gpp::ChangePin(const string& old_pin,
                                       const string& new_pin,
                                       Error* error,
                                       const ResultCallback& callback) {
  CHECK(error);
  sim_proxy_->ChangePin(old_pin, new_pin, error, callback, kTimeoutDefault);
}

void CellularCapability3gpp::Reset(Error* error,
                                   const ResultCallback& callback) {
  SLOG(this, 3) << __func__;
  CHECK(error);
  if (resetting_) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInProgress,
                          "Already resetting");
    return;
  }
  ResultCallback cb = Bind(&CellularCapability3gpp::OnResetReply,
                           weak_ptr_factory_.GetWeakPtr(), callback);
  modem_proxy_->Reset(error, cb, kTimeoutReset);
  if (!error->IsFailure()) {
    resetting_ = true;
  }
}

void CellularCapability3gpp::OnResetReply(const ResultCallback& callback,
                                          const Error& error) {
  SLOG(this, 3) << __func__;
  resetting_ = false;
  if (!callback.is_null())
    callback.Run(error);
}

void CellularCapability3gpp::Scan(Error* error,
                                  const ResultStringmapsCallback& callback) {
  KeyValueStoresCallback cb = Bind(&CellularCapability3gpp::OnScanReply,
                                   weak_ptr_factory_.GetWeakPtr(), callback);
  modem_3gpp_proxy_->Scan(error, cb, kTimeoutScan);
}

void CellularCapability3gpp::OnScanReply(
    const ResultStringmapsCallback& callback,
    const ScanResults& results,
    const Error& error) {
  Stringmaps found_networks;
  for (const auto& result : results)
    found_networks.push_back(ParseScanResult(result));
  callback.Run(found_networks, error);
}

Stringmap CellularCapability3gpp::ParseScanResult(const ScanResult& result) {
  /* ScanResults contain the following keys:

     "status"
     A MMModem3gppNetworkAvailability value representing network
     availability status, given as an unsigned integer (signature "u").
     This key will always be present.

     "operator-long"
     Long-format name of operator, given as a string value (signature
     "s"). If the name is unknown, this field should not be present.

     "operator-short"
     Short-format name of operator, given as a string value
     (signature "s"). If the name is unknown, this field should not
     be present.

     "operator-code"
     Mobile code of the operator, given as a string value (signature
     "s"). Returned in the format "MCCMNC", where MCC is the
     three-digit ITU E.212 Mobile Country Code and MNC is the two- or
     three-digit GSM Mobile Network Code. e.g. "31026" or "310260".

     "access-technology"
     A MMModemAccessTechnology value representing the generic access
     technology used by this mobile network, given as an unsigned
     integer (signature "u").
  */
  Stringmap parsed;

  if (result.Contains<uint32_t>(kStatusProperty)) {
    uint32_t status = result.Get<uint32_t>(kStatusProperty);
    // numerical values are taken from 3GPP TS 27.007 Section 7.3.
    static const char* const kStatusString[] = {
        "unknown",    // MM_MODEM_3GPP_NETWORK_AVAILABILITY_UNKNOWN
        "available",  // MM_MODEM_3GPP_NETWORK_AVAILABILITY_AVAILABLE
        "current",    // MM_MODEM_3GPP_NETWORK_AVAILABILITY_CURRENT
        "forbidden",  // MM_MODEM_3GPP_NETWORK_AVAILABILITY_FORBIDDEN
    };
    parsed[kStatusProperty] = kStatusString[status];
  }

  // MMModemAccessTechnology
  if (result.Contains<uint32_t>(kOperatorAccessTechnologyProperty)) {
    parsed[kTechnologyProperty] = AccessTechnologyToString(
        result.Get<uint32_t>(kOperatorAccessTechnologyProperty));
  }

  string operator_long, operator_short, operator_code;
  if (result.Contains<string>(kOperatorLongProperty))
    parsed[kLongNameProperty] = result.Get<string>(kOperatorLongProperty);
  if (result.Contains<string>(kOperatorShortProperty))
    parsed[kShortNameProperty] = result.Get<string>(kOperatorShortProperty);
  if (result.Contains<string>(kOperatorCodeProperty))
    parsed[kNetworkIdProperty] = result.Get<string>(kOperatorCodeProperty);

  // If the long name is not available but the network ID is, look up the long
  // name in the mobile provider database.
  if ((!base::Contains(parsed, kLongNameProperty) ||
       parsed[kLongNameProperty].empty()) &&
      base::Contains(parsed, kNetworkIdProperty)) {
    mobile_operator_info_->Reset();
    mobile_operator_info_->UpdateMCCMNC(parsed[kNetworkIdProperty]);
    if (mobile_operator_info_->IsMobileNetworkOperatorKnown() &&
        !mobile_operator_info_->operator_name().empty()) {
      parsed[kLongNameProperty] = mobile_operator_info_->operator_name();
    }
  }
  return parsed;
}

void CellularCapability3gpp::SetInitialEpsBearer(
    const KeyValueStore& properties,
    Error* error,
    const ResultCallback& callback) {
  SLOG(this, 3) << __func__;
  if (modem_3gpp_proxy_) {
    modem_3gpp_proxy_->SetInitialEpsBearerSettings(properties, error, callback,
                                                   kTimeoutSetInitialEpsBearer);
  } else {
    SLOG(this, 3) << __func__ << " skipping, no 3GPP proxy";
  }
}

void CellularCapability3gpp::OnSetInitialEpsBearerReply(const Error& error) {
  SLOG(this, 3) << __func__;
  if (error.IsFailure()) {
    SLOG(this, 2) << "Failed to set the 'attach APN' for the EPS bearer.";
  }
}

void CellularCapability3gpp::SetupLocation(uint32_t sources,
                                           bool signal_location,
                                           const ResultCallback& callback) {
  Error error;
  modem_location_proxy_->Setup(sources, signal_location, &error, callback,
                               kTimeoutSetupLocation);
}

void CellularCapability3gpp::SetupSignal(uint32_t rate,
                                         const ResultCallback& callback) {
  SLOG(this, 3) << __func__;
  Error error;
  modem_signal_proxy_->Setup(rate, &error, callback, kTimeoutSetupSignal);
}

void CellularCapability3gpp::OnSetupLocationReply(const Error& error) {
  SLOG(this, 3) << __func__;
  if (error.IsFailure()) {
    // Not fatal: most devices already enable this when
    // ModemManager starts. This failure is only likely for devices
    // which don't support location gathering.
    SLOG(this, 2) << "Failed to setup modem location capability.";
    return;
  }
}

void CellularCapability3gpp::OnSetupSignalReply(const Error& error) {
  SLOG(this, 3) << __func__;
  if (error.IsFailure()) {
    SLOG(this, 2) << "Failed to setup modem signal capability.";
    return;
  }
}

void CellularCapability3gpp::GetLocation(const StringCallback& callback) {
  BrilloAnyCallback cb = Bind(&CellularCapability3gpp::OnGetLocationReply,
                              weak_ptr_factory_.GetWeakPtr(), callback);
  Error error;
  modem_location_proxy_->GetLocation(&error, cb, kTimeoutGetLocation);
}

void CellularCapability3gpp::OnGetLocationReply(
    const StringCallback& callback,
    const std::map<uint32_t, brillo::Any>& results,
    const Error& error) {
  SLOG(this, 3) << __func__;
  if (error.IsFailure()) {
    SLOG(this, 2) << "Error getting location.";
    return;
  }
  // For 3G modems we currently only care about the "MCC,MNC,LAC,CI" location
  auto it = results.find(MM_MODEM_LOCATION_SOURCE_3GPP_LAC_CI);
  if (it != results.end()) {
    brillo::Any gpp_value = it->second;
    const string& location_string = gpp_value.Get<const string>();
    callback.Run(location_string, Error());
  } else {
    callback.Run(std::string(), Error());
  }
}

bool CellularCapability3gpp::IsLocationUpdateSupported() const {
  // Allow modems as they're tested / needed
  return cellular()->mm_plugin() == kTelitMMPlugin;
}

CellularBearer* CellularCapability3gpp::GetActiveBearer() const {
  return active_bearer_.get();
}

const std::vector<std::unique_ptr<MobileOperatorInfo::MobileAPN>>&
CellularCapability3gpp::GetProfiles() const {
  return profiles_;
}

string CellularCapability3gpp::GetNetworkTechnologyString() const {
  return AccessTechnologyToString(access_technologies_);
}

string CellularCapability3gpp::GetRoamingStateString() const {
  switch (registration_state_) {
    case MM_MODEM_3GPP_REGISTRATION_STATE_HOME:
      return kRoamingStateHome;
    case MM_MODEM_3GPP_REGISTRATION_STATE_ROAMING:
      return kRoamingStateRoaming;
    default:
      break;
  }
  return kRoamingStateUnknown;
}

string CellularCapability3gpp::GetTypeString() const {
  return AccessTechnologyToTechnologyFamily(access_technologies_);
}

void CellularCapability3gpp::OnModemPropertiesChanged(
    const KeyValueStore& properties) {
  SLOG(this, 3) << __func__;

  // Update the bearers property before the modem state property as
  // OnModemStateChanged may call UpdateActiveBearer, which reads the bearers
  // property.
  if (properties.Contains<RpcIdentifiers>(MM_MODEM_PROPERTY_BEARERS)) {
    RpcIdentifiers bearers =
        properties.Get<RpcIdentifiers>(MM_MODEM_PROPERTY_BEARERS);
    OnBearersChanged(bearers);
  }

  // This solves a bootstrapping problem: If the modem is not yet
  // enabled, there are no proxy objects associated with the capability
  // object, so modem signals like StateChanged aren't seen. By monitoring
  // changes to the State property via the ModemManager, we're able to
  // get the initialization process started, which will result in the
  // creation of the proxy objects.
  //
  // The first time we see the change to State (when the modem state
  // is Unknown), we simply update the state, and rely on the Manager to
  // enable the device when it is registered with the Manager. On subsequent
  // changes to State, we need to explicitly enable the device ourselves.
  if (properties.Contains<int32_t>(MM_MODEM_PROPERTY_STATE)) {
    int32_t istate = properties.Get<int32_t>(MM_MODEM_PROPERTY_STATE);
    Cellular::ModemState state = static_cast<Cellular::ModemState>(istate);
    OnModemStateChanged(state);
  }

  // dbus_properties_proxy_->GetAll(MM_DBUS_INTERFACE_MODEM) may not return all
  // properties, so only update SIM properties if SIM or SIMSLOTS was provided.
  bool sim_changed = false;
  if (properties.Contains<RpcIdentifier>(MM_MODEM_PROPERTY_SIM)) {
    sim_path_ = properties.Get<RpcIdentifier>(MM_MODEM_PROPERTY_SIM);
    sim_changed = true;
  }
  if (properties.Contains<RpcIdentifiers>(MM_MODEM_PROPERTY_SIMSLOTS)) {
    sim_slots_ = properties.Get<RpcIdentifiers>(MM_MODEM_PROPERTY_SIMSLOTS);
    sim_changed = true;
  }
  if (sim_changed)
    UpdateSims();

  if (properties.Contains<uint32_t>(MM_MODEM_PROPERTY_CURRENTCAPABILITIES)) {
    OnModemCurrentCapabilitiesChanged(
        properties.Get<uint32_t>(MM_MODEM_PROPERTY_CURRENTCAPABILITIES));
  }
  if (properties.Contains<string>(MM_MODEM_PROPERTY_MANUFACTURER)) {
    cellular()->set_manufacturer(
        properties.Get<string>(MM_MODEM_PROPERTY_MANUFACTURER));
  }
  if (properties.Contains<string>(MM_MODEM_PROPERTY_MODEL)) {
    cellular()->set_model_id(properties.Get<string>(MM_MODEM_PROPERTY_MODEL));
  }
  if (properties.Contains<string>(MM_MODEM_PROPERTY_PLUGIN)) {
    cellular()->set_mm_plugin(properties.Get<string>(MM_MODEM_PROPERTY_PLUGIN));
  }
  if (properties.Contains<string>(MM_MODEM_PROPERTY_REVISION)) {
    cellular()->set_firmware_revision(
        properties.Get<string>(MM_MODEM_PROPERTY_REVISION));
  }
  if (properties.Contains<string>(MM_MODEM_PROPERTY_HARDWAREREVISION)) {
    cellular()->set_hardware_revision(
        properties.Get<string>(MM_MODEM_PROPERTY_HARDWAREREVISION));
  }
  if (properties.Contains<string>(MM_MODEM_PROPERTY_DEVICE)) {
    std::string path = properties.Get<string>(MM_MODEM_PROPERTY_DEVICE);
    cellular()->set_device_id(DeviceId::CreateFromSysfs(base::FilePath(path)));
  }
  if (properties.Contains<string>(MM_MODEM_PROPERTY_EQUIPMENTIDENTIFIER)) {
    cellular()->set_equipment_id(
        properties.Get<string>(MM_MODEM_PROPERTY_EQUIPMENTIDENTIFIER));
  }

  // Unlock required and SimLock
  bool lock_status_changed = false;
  if (properties.Contains<uint32_t>(MM_MODEM_PROPERTY_UNLOCKREQUIRED)) {
    uint32_t unlock_required =
        properties.Get<uint32_t>(MM_MODEM_PROPERTY_UNLOCKREQUIRED);
    OnLockTypeChanged(static_cast<MMModemLock>(unlock_required));
    lock_status_changed = true;
  }

  // Unlock retries
  if (properties.ContainsVariant(MM_MODEM_PROPERTY_UNLOCKRETRIES)) {
    OnLockRetriesChanged(properties.GetVariant(MM_MODEM_PROPERTY_UNLOCKRETRIES)
                             .Get<LockRetryData>());
    lock_status_changed = true;
  }

  if (lock_status_changed)
    OnSimLockStatusChanged();

  if (properties.Contains<uint32_t>(MM_MODEM_PROPERTY_ACCESSTECHNOLOGIES)) {
    OnAccessTechnologiesChanged(
        properties.Get<uint32_t>(MM_MODEM_PROPERTY_ACCESSTECHNOLOGIES));
  }

  if (properties.Contains<Strings>(MM_MODEM_PROPERTY_OWNNUMBERS)) {
    vector<string> numbers =
        properties.Get<Strings>(MM_MODEM_PROPERTY_OWNNUMBERS);
    string mdn;
    if (!numbers.empty())
      mdn = numbers[0];
    OnMdnChanged(mdn);
  }
}

void CellularCapability3gpp::OnPropertiesChanged(
    const string& interface, const KeyValueStore& changed_properties) {
  if (interface == MM_DBUS_INTERFACE_MODEM) {
    OnModemPropertiesChanged(changed_properties);
  }
  if (interface == MM_DBUS_INTERFACE_MODEM_MODEM3GPP) {
    OnModem3gppPropertiesChanged(changed_properties);
  }
  if (interface == MM_DBUS_INTERFACE_MODEM_SIGNAL) {
    OnModemSignalPropertiesChanged(changed_properties);
  }
  if (interface == MM_DBUS_INTERFACE_SIM) {
    // A SIM change will generate a new Modem object so we do not expect a SIM
    // interface change event.
    LOG(ERROR) << "Unexpected SIM properties change, ignoring";
  }
}

bool CellularCapability3gpp::RetriableConnectError(const Error& error) const {
  return error.type() == Error::kInvalidApn;
}

string CellularCapability3gpp::NormalizeMdn(const string& mdn) const {
  string normalized_mdn;
  for (size_t i = 0; i < mdn.size(); ++i) {
    if (base::IsAsciiDigit(mdn[i]))
      normalized_mdn += mdn[i];
  }
  return normalized_mdn;
}

bool CellularCapability3gpp::IsValidSimPath(
    const RpcIdentifier& sim_path) const {
  return !sim_path.value().empty() && sim_path != kRootPath;
}

void CellularCapability3gpp::UpdateSims() {
  LOG(INFO) << __func__ << " Sim path: " << sim_path_.value()
            << " SimSlots: " << sim_slots_.size();

  // Clear current properties and requests.
  sim_properties_.clear();
  pending_sim_requests_.clear();

  // Ensure |sim_slots_| has a SIM (use |sim_path_| if slots property is empty).
  if (sim_slots_.empty()) {
    if (!IsValidSimPath(sim_path_)) {
      LOG(WARNING) << "No valid SIM path or SIMSLOTS";
      OnAllSimPropertiesReceived();
      return;
    }
    // No SIMSLOTS property, use SIM path only.
    sim_slots_.push_back(sim_path_);
  }

  // Build the list of pending requests first so that RequestSimProperties()
  // won't call OnAllSimPropertiesReceived() early (e.g. in tests).
  std::vector<std::pair<size_t, RpcIdentifier>> sim_requests;
  for (size_t i = 0; i < sim_slots_.size(); ++i) {
    const RpcIdentifier& path = sim_slots_[i];
    if (!IsValidSimPath(path)) {
      LOG(WARNING) << "Invalid slot path: " << path.value();
      continue;
    }
    sim_requests.push_back(std::make_pair(i, path));
    pending_sim_requests_.insert(path);
  }
  if (sim_requests.empty()) {
    LOG(WARNING) << "No valid SIM slots.";
    OnAllSimPropertiesReceived();
    return;
  }

  // Request the SIM properties for each slot.
  for (const auto& request : sim_requests)
    RequestSimProperties(request.first, request.second);
}

void CellularCapability3gpp::OnAllSimPropertiesReceived() {
  SLOG(this, 1) << __func__ << " Primary Sim path=" << sim_path_.value();
  if (IsValidSimPath(sim_path_)) {
    sim_proxy_ = control_interface()->CreateMM1SimProxy(
        sim_path_, cellular()->dbus_service());
  } else {
    sim_proxy_ = nullptr;
  }

  // Ensure that the primary SIM slot is set correctly.
  const SimProperties* primary_sim_properties = nullptr;
  auto iter = sim_properties_.find(sim_path_);
  if (iter != sim_properties_.end())
    primary_sim_properties = &iter->second;
  if (!primary_sim_properties || primary_sim_properties->iccid.empty()) {
    // Check secondary SIM slots for a non empty ICCID.
    for (const auto& iter : sim_properties_) {
      if (iter.first == sim_path_)
        continue;
      const SimProperties* properties = &iter.second;
      if (!properties->iccid.empty()) {
        // This will complete immediately, at which point the Modem object will
        // become invalid. TODO(b/169581681): Ensure this is handled gracefully.
        SetPrimarySimSlot(properties->slot);
        return;
      }
    }
  }

  // Update SIM properties for the primary SIM slot.
  if (primary_sim_properties) {
    SetPrimarySimProperties(*primary_sim_properties);
  } else {
    LOG(INFO) << " No Primary SIM properties.";
    SetPrimarySimProperties(SimProperties());
  }
  // Update SIM slot properties for each SIM slot. Slots with an empty path
  // will contain an empty SimProperties entry.
  size_t num_slots = sim_slots_.size();
  std::vector<SimProperties> sim_slot_properties(num_slots);
  for (const auto& iter : sim_properties_) {
    size_t slot = iter.second.slot;
    DCHECK_GE(slot, 0u);
    DCHECK_LT(slot, num_slots);
    sim_slot_properties[slot] = iter.second;
  }
  cellular()->SetSimSlotProperties(sim_slot_properties);
}

void CellularCapability3gpp::SetPrimarySimProperties(
    const SimProperties& sim_properties) {
  cellular()->SetPrimarySimProperties(sim_properties);

  UpdateServiceActivationState();
  UpdatePendingActivationState();

  cellular()->home_provider_info()->UpdateMCCMNC(sim_properties.operator_id);
  spn_ = sim_properties.spn;
  cellular()->home_provider_info()->UpdateOperatorName(spn_);
}

void CellularCapability3gpp::SetPrimarySimSlot(size_t slot) {
  size_t slot_idx = slot + 1;
  LOG(INFO) << "SetPrimarySimSlot: " << slot_idx;
  modem_proxy_->SetPrimarySimSlot(
      slot_idx, base::Bind([](const Error& error) {
        LOG(ERROR) << "Error Setting Primary SIM slot: " << error;
      }),
      kTimeoutDefault);
}

void CellularCapability3gpp::OnModemCurrentCapabilitiesChanged(
    uint32_t current_capabilities) {
  if (current_capabilities == current_capabilities_)
    return;

  SLOG(this, 2) << __func__;
  current_capabilities_ = current_capabilities;

  // Only allow network scan when the modem's current capabilities support
  // GSM/UMTS.
  //
  // TODO(benchan): We should consider having the modem plugins in ModemManager
  // reporting whether network scan is supported.
  cellular()->set_scanning_supported(
      (current_capabilities & MM_MODEM_CAPABILITY_GSM_UMTS) != 0);
}

void CellularCapability3gpp::OnMdnChanged(const string& mdn) {
  std::string normalized_mdn = NormalizeMdn(mdn);
  if (cellular()->mdn() == normalized_mdn)
    return;

  SLOG(this, 2) << __func__ << ": " << normalized_mdn;
  cellular()->set_mdn(normalized_mdn);
  UpdateServiceActivationState();
  UpdatePendingActivationState();
}

void CellularCapability3gpp::OnModemStateChanged(Cellular::ModemState state) {
  SLOG(this, 1) << __func__ << ": " << Cellular::GetModemStateString(state);

  if (state == Cellular::kModemStateConnected) {
    // This assumes that ModemManager updates the Bearers list and the Bearer
    // properties before changing Modem state to Connected.
    SLOG(this, 2) << "Update active bearer.";
    UpdateActiveBearer();
  }

  cellular()->OnModemStateChanged(state);
}

void CellularCapability3gpp::OnAccessTechnologiesChanged(
    uint32_t access_technologies) {
  if (access_technologies_ == access_technologies)
    return;

  SLOG(this, 2) << __func__;
  const string old_type_string(GetTypeString());
  access_technologies_ = access_technologies;
  const string new_type_string(GetTypeString());
  if (new_type_string != old_type_string) {
    cellular()->adaptor()->EmitStringChanged(kTechnologyFamilyProperty,
                                             new_type_string);
  }
  if (cellular()->service().get()) {
    cellular()->service()->SetNetworkTechnology(GetNetworkTechnologyString());
  }
}

void CellularCapability3gpp::OnBearersChanged(const RpcIdentifiers& bearers) {
  if (bearers == bearer_paths_)
    return;

  SLOG(this, 2) << __func__;
  bearer_paths_ = bearers;
}

void CellularCapability3gpp::OnLockRetriesChanged(
    const LockRetryData& lock_retries) {
  SLOG(this, 3) << __func__;

  // UI uses lock_retries to indicate the number of attempts remaining
  // for enable pin/disable pin/change pin
  // By default, the UI operates on PIN1, thus lock_retries should return
  // number of PIN1 retries. The only exception is PUK lock, where the UI needs
  // to report the number of PUK retries.
  // TODO(pholla): Personalization requires the UI to display multiple locks,
  // so shill needs to communicate an array of sim_lock_status (b/169615875)
  auto retry_lock_type = (sim_lock_status_.lock_type == MM_MODEM_LOCK_SIM_PUK)
                             ? MM_MODEM_LOCK_SIM_PUK
                             : MM_MODEM_LOCK_SIM_PIN;
  auto it = lock_retries.find(retry_lock_type);

  sim_lock_status_.retries_left =
      (it != lock_retries.end()) ? it->second : kUnknownLockRetriesLeft;
}

void CellularCapability3gpp::OnLockTypeChanged(MMModemLock lock_type) {
  SLOG(this, 3) << __func__ << ": " << lock_type;
  sim_lock_status_.lock_type = lock_type;

  // If the SIM is in a locked state |sim_lock_status_.enabled| might be false.
  // This is because the corresponding property 'EnabledFacilityLocks' is on
  // the 3GPP interface and the 3GPP interface is not available while the Modem
  // is in the 'LOCKED' state.
  if (lock_type != MM_MODEM_LOCK_NONE && lock_type != MM_MODEM_LOCK_UNKNOWN &&
      !sim_lock_status_.enabled)
    sim_lock_status_.enabled = true;
}

void CellularCapability3gpp::OnSimLockStatusChanged() {
  SLOG(this, 2) << __func__;
  cellular()->adaptor()->EmitKeyValueStoreChanged(
      kSIMLockStatusProperty, SimLockStatusToProperty(nullptr));

  if (!IsValidSimPath(sim_path_) ||
      (sim_lock_status_.lock_type != MM_MODEM_LOCK_NONE &&
       sim_lock_status_.lock_type != MM_MODEM_LOCK_UNKNOWN)) {
    return;
  }

  // If the SIM is currently unlocked, assume that we need to refresh
  // carrier information, since a locked SIM prevents shill from obtaining
  // the necessary data to establish a connection later (e.g. IMSI).
  UpdateSims();
}

void CellularCapability3gpp::OnModem3gppPropertiesChanged(
    const KeyValueStore& properties) {
  SLOG(this, 3) << __func__;
  if (properties.Contains<string>(MM_MODEM_MODEM3GPP_PROPERTY_IMEI)) {
    cellular()->SetImei(
        properties.Get<string>(MM_MODEM_MODEM3GPP_PROPERTY_IMEI));
  }
  // Handle registration state changes as a single change
  Stringmap::const_iterator it;
  string operator_code;
  string operator_name;
  it = serving_operator_.find(kOperatorCodeKey);
  if (it != serving_operator_.end())
    operator_code = it->second;
  it = serving_operator_.find(kOperatorNameKey);
  if (it != serving_operator_.end())
    operator_name = it->second;

  MMModem3gppRegistrationState state = registration_state_;
  bool registration_changed = false;
  if (properties.Contains<uint32_t>(
          MM_MODEM_MODEM3GPP_PROPERTY_REGISTRATIONSTATE)) {
    state = static_cast<MMModem3gppRegistrationState>(properties.Get<uint32_t>(
        MM_MODEM_MODEM3GPP_PROPERTY_REGISTRATIONSTATE));
    registration_changed = true;
  }
  if (properties.Contains<string>(MM_MODEM_MODEM3GPP_PROPERTY_OPERATORCODE)) {
    operator_code =
        properties.Get<string>(MM_MODEM_MODEM3GPP_PROPERTY_OPERATORCODE);
    registration_changed = true;
  }
  if (properties.Contains<string>(MM_MODEM_MODEM3GPP_PROPERTY_OPERATORNAME)) {
    operator_name =
        properties.Get<string>(MM_MODEM_MODEM3GPP_PROPERTY_OPERATORNAME);
    registration_changed = true;
  }
  if (registration_changed)
    On3gppRegistrationChanged(state, operator_code, operator_name);

  if (properties.Contains<uint32_t>(
          MM_MODEM_MODEM3GPP_PROPERTY_ENABLEDFACILITYLOCKS))
    OnFacilityLocksChanged(properties.Get<uint32_t>(
        MM_MODEM_MODEM3GPP_PROPERTY_ENABLEDFACILITYLOCKS));

  if (properties.ContainsVariant(MM_MODEM_MODEM3GPP_PROPERTY_PCO)) {
    OnPcoChanged(
        properties.GetVariant(MM_MODEM_MODEM3GPP_PROPERTY_PCO).Get<PcoList>());
  }

  if (properties.ContainsVariant(MM_MODEM_MODEM3GPP_PROPERTY_PROFILES)) {
    OnProfilesChanged(
        properties.GetVariant(MM_MODEM_MODEM3GPP_PROPERTY_PROFILES)
            .Get<Profiles>());
  }
}

void CellularCapability3gpp::OnProfilesChanged(const Profiles& profiles) {
  profiles_.clear();
  for (const auto& profile : profiles) {
    auto apn_info = std::make_unique<MobileOperatorInfo::MobileAPN>();
    apn_info->apn =
        brillo::GetVariantValueOrDefault<string>(profile, kProfileApn);
    apn_info->username =
        brillo::GetVariantValueOrDefault<string>(profile, kProfileUsername);
    apn_info->password =
        brillo::GetVariantValueOrDefault<string>(profile, kProfilePassword);
    apn_info->authentication =
        MMBearerAllowedAuthToApnAuthentication(static_cast<MMBearerAllowedAuth>(
            brillo::GetVariantValueOrDefault<uint32_t>(profile,
                                                       kProfileAuthType)));
    profiles_.push_back(std::move(apn_info));
  }

  // The cellular object may need to update the APN list now.
  cellular()->OnOperatorChanged();

  // Bail-out early if we don't want to setup the attach APN
  if (!cellular()->use_attach_apn())
    return;

  // Set the new parameters for the initial EPS bearer (e.g. LTE Attach APN)
  KeyValueStore properties;
  Error error;
  FillInitialEpsBearerPropertyMap(&properties);
  ResultCallback cb = Bind(&CellularCapability3gpp::OnSetInitialEpsBearerReply,
                           weak_ptr_factory_.GetWeakPtr());
  // If 'properties' is empty, this will clear the 'attach APN' on the modem.
  SetInitialEpsBearer(properties, &error, cb);
}

void CellularCapability3gpp::On3gppRegistrationChanged(
    MMModem3gppRegistrationState state,
    const string& operator_code,
    const string& operator_name) {
  SLOG(this, 2) << __func__ << ": " << RegistrationStateToString(state);
  SLOG(this, 3) << "opercode=" << operator_code
                << ", opername=" << operator_name;

  // While the modem is connected, if the state changed from a registered state
  // to a non registered state, defer the state change by 15 seconds.
  if (cellular()->modem_state() == Cellular::kModemStateConnected &&
      IsRegistered() && !IsRegisteredState(state)) {
    if (!registration_dropped_update_callback_.IsCancelled()) {
      LOG(WARNING) << "Modem reported consecutive 3GPP registration drops. "
                   << "Ignoring earlier notifications.";
      registration_dropped_update_callback_.Cancel();
    } else {
      // This is not a repeated post. So, count this instance of delayed drop
      // posted.
      metrics_->Notify3GPPRegistrationDelayedDropPosted();
    }
    SLOG(this, 2) << "Posted deferred registration state update";
    registration_dropped_update_callback_.Reset(Bind(
        &CellularCapability3gpp::Handle3gppRegistrationChange,
        weak_ptr_factory_.GetWeakPtr(), state, operator_code, operator_name));
    cellular()->dispatcher()->PostDelayedTask(
        FROM_HERE, registration_dropped_update_callback_.callback(),
        registration_dropped_update_timeout_milliseconds_);
  } else {
    if (!registration_dropped_update_callback_.IsCancelled()) {
      SLOG(this, 2) << "Cancelled a deferred registration state update";
      registration_dropped_update_callback_.Cancel();
      // If we cancelled the callback here, it means we had flaky network for a
      // small duration.
      metrics_->Notify3GPPRegistrationDelayedDropCanceled();
    }
    Handle3gppRegistrationChange(state, operator_code, operator_name);
  }
}

void CellularCapability3gpp::Handle3gppRegistrationChange(
    MMModem3gppRegistrationState updated_state,
    const string& updated_operator_code,
    const string& updated_operator_name) {
  SLOG(this, 2) << __func__ << ": " << RegistrationStateToString(updated_state);

  registration_state_ = updated_state;
  serving_operator_[kOperatorCodeKey] = updated_operator_code;
  serving_operator_[kOperatorNameKey] = updated_operator_name;
  cellular()->serving_operator_info()->UpdateMCCMNC(updated_operator_code);
  cellular()->serving_operator_info()->UpdateOperatorName(
      updated_operator_name);

  cellular()->HandleNewRegistrationState();

  // A finished callback does not qualify as a canceled callback.
  // We test for a canceled callback to check for outstanding callbacks.
  // So, explicitly cancel the callback here.
  // Caution: Do not use any function arguments post the call to Cancel().
  // Cancel() call invalidates the arguments that were copied when creating
  // the callback.
  registration_dropped_update_callback_.Cancel();

  // If the modem registered with the network and the current ICCID is pending
  // activation, then reset the modem.
  UpdatePendingActivationState();
}

void CellularCapability3gpp::OnSubscriptionStateChanged(
    SubscriptionState updated_subscription_state) {
  SLOG(this, 3) << __func__ << ": Updated subscription state = "
                << SubscriptionStateToString(updated_subscription_state);

  if (updated_subscription_state == subscription_state_)
    return;

  subscription_state_ = updated_subscription_state;

  UpdateServiceActivationState();
  UpdatePendingActivationState();
}

void CellularCapability3gpp::OnModemStateChangedSignal(int32_t old_state,
                                                       int32_t new_state,
                                                       uint32_t reason) {
  Cellular::ModemState old_modem_state =
      static_cast<Cellular::ModemState>(old_state);
  Cellular::ModemState new_modem_state =
      static_cast<Cellular::ModemState>(new_state);
  SLOG(this, 3) << __func__ << "("
                << Cellular::GetModemStateString(old_modem_state) << ", "
                << Cellular::GetModemStateString(new_modem_state) << ", "
                << reason << ")";
}

void CellularCapability3gpp::OnFacilityLocksChanged(uint32_t locks) {
  bool sim_enabled = !!(locks & MM_MODEM_3GPP_FACILITY_SIM);
  if (sim_lock_status_.enabled != sim_enabled) {
    sim_lock_status_.enabled = sim_enabled;
    OnSimLockStatusChanged();
  }
}

void CellularCapability3gpp::OnPcoChanged(const PcoList& pco_list) {
  SLOG(this, 3) << __func__;

  for (const auto& pco_info : pco_list) {
    uint32_t session_id = std::get<0>(pco_info);
    bool is_complete = std::get<1>(pco_info);
    vector<uint8_t> data = std::get<2>(pco_info);

    SLOG(this, 3) << "PCO: session-id=" << session_id
                  << ", complete=" << is_complete
                  << ", data=" << base::HexEncode(data.data(), data.size())
                  << "";

    std::unique_ptr<CellularPco> pco = CellularPco::CreateFromRawData(data);
    if (!pco) {
      LOG(WARNING) << "Failed to parse PCO (session-id " << session_id << ")";
      continue;
    }

    SubscriptionState subscription_state = SubscriptionState::kUnknown;
    if (!FindVerizonSubscriptionStateFromPco(*pco, &subscription_state))
      continue;

    if (subscription_state != SubscriptionState::kUnknown)
      OnSubscriptionStateChanged(subscription_state);
  }
}

// Chrome OS UI uses signal quality values set by this method to draw
// network icons. UI code maps |quality| to number of bars as follows:
// [1-25] 1 bar, [26-50] 2 bars, [51-75] 3 bars and [76-100] 4 bars.
// -128->-88 rsrp scales to UI quality of 0->100, used for 4G
// -105->-83 rssi scales to UI quality of 0->100, used for other tech
void CellularCapability3gpp::OnModemSignalPropertiesChanged(
    const KeyValueStore& props) {
  SLOG(this, 3) << __func__;
  uint32_t scaled_quality = 0;
  // Technologies whose signal strength will be probed, ordered by priority
  std::vector<std::string> signal_properties_list = {
      MM_MODEM_SIGNAL_PROPERTY_LTE, MM_MODEM_SIGNAL_PROPERTY_UMTS,
      MM_MODEM_SIGNAL_PROPERTY_GSM, MM_MODEM_SIGNAL_PROPERTY_CDMA,
      MM_MODEM_SIGNAL_PROPERTY_EVDO};
  for (auto signal_property : signal_properties_list) {
    if (props.ContainsVariant(signal_property)) {
      auto tech_props = props.GetVariant(signal_property).Get<KeyValueStore>();
      double signal_quality = 0.0;

      if (tech_props.Contains<double>(kRsrpProperty)) {
        signal_quality = tech_props.Get<double>(kRsrpProperty);
        scaled_quality = kRsrpBounds.GetAsPercentage(signal_quality);
      } else if (tech_props.Contains<double>(kRssiProperty)) {
        signal_quality = tech_props.Get<double>(kRssiProperty);
        scaled_quality = kRssiBounds.GetAsPercentage(signal_quality);
      } else {
        // we aren't interested in this tech since it does not report rssi/rsrp
        continue;
      }

      SLOG(this, 4) << "signal_quality:" << signal_quality
                    << " scaled_quality:" << scaled_quality;
      cellular()->HandleNewSignalQuality(scaled_quality);
      // we've found a signal quality indicator, no need to parse other
      // technologies.
      return;
    }
  }
}

void CellularCapability3gpp::RequestSimProperties(size_t slot,
                                                  RpcIdentifier sim_path) {
  LOG(INFO) << __func__ << ": " << slot << ": " << sim_path.value();
  // Ownership if this proxy will be passed to the success callback so that the
  // proxy is not destroyed before the asynchronous call completes.
  std::unique_ptr<DBusPropertiesProxy> sim_properties_proxy =
      control_interface()->CreateDBusPropertiesProxy(
          sim_path, cellular()->dbus_service());
  DBusPropertiesProxy* sim_properties_proxy_ptr = sim_properties_proxy.get();
  sim_properties_proxy_ptr->GetAllAsync(
      MM_DBUS_INTERFACE_SIM,
      base::Bind(&CellularCapability3gpp::OnGetSimProperties,
                 weak_ptr_factory_.GetWeakPtr(), slot, sim_path,
                 base::Passed(&sim_properties_proxy)),
      base::Bind([](const Error& error) {
        LOG(ERROR) << "Error fetching SIM properties: " << error;
      }));
}

void CellularCapability3gpp::OnGetSimProperties(
    size_t slot,
    RpcIdentifier sim_path,
    std::unique_ptr<DBusPropertiesProxy> sim_properties_proxy,
    const KeyValueStore& properties) {
  SLOG(this, 2) << __func__ << ": " << slot << ": " << sim_path.value();
  SimProperties sim_properties;
  sim_properties.slot = slot;
  if (properties.Contains<string>(MM_SIM_PROPERTY_SIMIDENTIFIER)) {
    sim_properties.iccid =
        properties.Get<string>(MM_SIM_PROPERTY_SIMIDENTIFIER);
  }
  if (properties.Contains<string>(MM_SIM_PROPERTY_EID)) {
    sim_properties.eid = properties.Get<string>(MM_SIM_PROPERTY_EID);
  }
  if (properties.Contains<string>(MM_SIM_PROPERTY_OPERATORIDENTIFIER)) {
    sim_properties.operator_id =
        properties.Get<string>(MM_SIM_PROPERTY_OPERATORIDENTIFIER);
  }
  if (properties.Contains<string>(MM_SIM_PROPERTY_OPERATORNAME)) {
    sim_properties.spn = (properties.Get<string>(MM_SIM_PROPERTY_OPERATORNAME));
  }
  if (properties.Contains<string>(MM_SIM_PROPERTY_IMSI)) {
    sim_properties.imsi = properties.Get<string>(MM_SIM_PROPERTY_IMSI);
  }
  sim_properties_[sim_path] = sim_properties;
  pending_sim_requests_.erase(sim_path);
  if (pending_sim_requests_.empty())
    OnAllSimPropertiesReceived();

  // |sim_properties_proxy| will be safely released here.
}

double CellularCapability3gpp::SignalQualityBounds::GetAsPercentage(
    double signal_quality) const {
  double clamped_signal_quality =
      std::min(std::max(signal_quality, min_threshold), max_threshold);

  return (clamped_signal_quality - min_threshold) * 100 /
         (max_threshold - min_threshold);
}
}  // namespace shill
