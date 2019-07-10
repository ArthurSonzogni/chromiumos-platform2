// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_capability_cdma.h"

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/cellular/cellular_bearer.h"
#include "shill/cellular/cellular_service.h"
#include "shill/control_interface.h"
#include "shill/dbus_properties_proxy_interface.h"
#include "shill/error.h"
#include "shill/logging.h"
#include "shill/pending_activation_store.h"

using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCellular;
static string ObjectID(CellularCapabilityCdma* c) {
  return c->cellular()->GetRpcIdentifier();
}
}  // namespace Logging

namespace {

string GetActivationStateString(uint32_t state) {
  switch (state) {
    case MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATED:
      return kActivationStateActivated;
    case MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATING:
      return kActivationStateActivating;
    case MM_MODEM_CDMA_ACTIVATION_STATE_NOT_ACTIVATED:
      return kActivationStateNotActivated;
    case MM_MODEM_CDMA_ACTIVATION_STATE_PARTIALLY_ACTIVATED:
      return kActivationStatePartiallyActivated;
    default:
      return kActivationStateUnknown;
  }
}

string GetActivationErrorString(uint32_t error) {
  switch (error) {
    case MM_CDMA_ACTIVATION_ERROR_WRONG_RADIO_INTERFACE:
      return kErrorNeedEvdo;
    case MM_CDMA_ACTIVATION_ERROR_ROAMING:
      return kErrorNeedHomeNetwork;
    case MM_CDMA_ACTIVATION_ERROR_COULD_NOT_CONNECT:
    case MM_CDMA_ACTIVATION_ERROR_SECURITY_AUTHENTICATION_FAILED:
    case MM_CDMA_ACTIVATION_ERROR_PROVISIONING_FAILED:
      return kErrorOtaspFailed;
    case MM_CDMA_ACTIVATION_ERROR_NONE:
      return "";
    case MM_CDMA_ACTIVATION_ERROR_NO_SIGNAL:
    default:
      return kErrorActivationFailed;
  }
}

}  // namespace

CellularCapabilityCdma::CellularCapabilityCdma(Cellular* cellular,
                                               ModemInfo* modem_info)
    : CellularCapability3gpp(cellular, modem_info),
      activation_state_(MM_MODEM_CDMA_ACTIVATION_STATE_NOT_ACTIVATED),
      cdma_1x_registration_state_(MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN),
      cdma_evdo_registration_state_(MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN),
      nid_(0),
      sid_(0),
      weak_cdma_ptr_factory_(this) {
  SLOG(this, 2) << "Cellular capability constructed: CDMA";
  // TODO(armansito): Update PRL for activation over cellular.
  // See crbug.com/197330.
}

CellularCapabilityCdma::~CellularCapabilityCdma() = default;

void CellularCapabilityCdma::InitProxies() {
  SLOG(this, 2) << __func__;
  modem_cdma_proxy_ = control_interface()->CreateMM1ModemModemCdmaProxy(
      cellular()->dbus_path(), cellular()->dbus_service());
  modem_cdma_proxy_->set_activation_state_callback(
      Bind(&CellularCapabilityCdma::OnActivationStateChangedSignal,
           weak_cdma_ptr_factory_.GetWeakPtr()));
  CellularCapability3gpp::InitProxies();
}

void CellularCapabilityCdma::ReleaseProxies() {
  SLOG(this, 2) << __func__;
  modem_cdma_proxy_.reset();
  CellularCapability3gpp::ReleaseProxies();
}

void CellularCapabilityCdma::CompleteActivation(Error* error) {
  SLOG(this, 2) << __func__;
  if (cellular()->state() < Cellular::kStateEnabled) {
    Error::PopulateAndLog(FROM_HERE, error, Error::kInvalidArguments,
                          "Unable to activate in state " +
                              Cellular::GetStateString(cellular()->state()));
    return;
  }
  ActivateAutomatic();
}

void CellularCapabilityCdma::ActivateAutomatic() {
  if (!cellular()->serving_operator_info()->IsMobileNetworkOperatorKnown() ||
      cellular()->serving_operator_info()->activation_code().empty()) {
    SLOG(this, 2) << "OTA activation cannot be run in the presence of no "
                  << "activation code.";
    return;
  }

  PendingActivationStore::State state =
      modem_info()->pending_activation_store()->GetActivationState(
          PendingActivationStore::kIdentifierMEID, cellular()->meid());
  if (state == PendingActivationStore::kStatePending) {
    SLOG(this, 2) << "There's already a pending activation. Ignoring.";
    return;
  }
  if (state == PendingActivationStore::kStateActivated) {
    SLOG(this, 2) << "A call to OTA activation has already completed "
                  << "successfully. Ignoring.";
    return;
  }

  // Mark as pending activation, so that shill can recover if anything fails
  // during OTA activation.
  modem_info()->pending_activation_store()->SetActivationState(
      PendingActivationStore::kIdentifierMEID, cellular()->meid(),
      PendingActivationStore::kStatePending);

  // Initiate OTA activation.
  ResultCallback activation_callback =
      Bind(&CellularCapabilityCdma::OnActivateReply,
           weak_cdma_ptr_factory_.GetWeakPtr(), ResultCallback());

  Error error;
  modem_cdma_proxy_->Activate(
      cellular()->serving_operator_info()->activation_code(), &error,
      activation_callback, kTimeoutActivate);
}

void CellularCapabilityCdma::UpdatePendingActivationState() {
  SLOG(this, 2) << __func__;
  if (IsActivated()) {
    SLOG(this, 3) << "CDMA service activated. Clear store.";
    modem_info()->pending_activation_store()->RemoveEntry(
        PendingActivationStore::kIdentifierMEID, cellular()->meid());
    return;
  }
  PendingActivationStore::State state =
      modem_info()->pending_activation_store()->GetActivationState(
          PendingActivationStore::kIdentifierMEID, cellular()->meid());
  if (IsActivating() && state != PendingActivationStore::kStateFailureRetry) {
    SLOG(this, 3) << "OTA activation in progress. Nothing to do.";
    return;
  }
  switch (state) {
    case PendingActivationStore::kStateFailureRetry:
      SLOG(this, 3) << "OTA activation failed. Scheduling a retry.";
      cellular()->dispatcher()->PostTask(
          FROM_HERE, Bind(&CellularCapabilityCdma::ActivateAutomatic,
                          weak_cdma_ptr_factory_.GetWeakPtr()));
      break;
    case PendingActivationStore::kStateActivated:
      SLOG(this, 3) << "OTA Activation has completed successfully. "
                    << "Waiting for activation state update to finalize.";
      break;
    default:
      break;
  }
}

bool CellularCapabilityCdma::AreProxiesInitialized() const {
  return modem_cdma_proxy_ != nullptr &&
         CellularCapability3gpp::AreProxiesInitialized();
}

bool CellularCapabilityCdma::IsServiceActivationRequired() const {
  // If there is no online payment portal information, it's safer to assume
  // the service does not require activation.
  if (!cellular()->serving_operator_info()->IsMobileNetworkOperatorKnown() ||
      cellular()->serving_operator_info()->olp_list().empty()) {
    return false;
  }

  // We could also use the MDN to determine whether or not the service is
  // activated, however, the CDMA ActivatonState property is a more absolute
  // and fine-grained indicator of activation status.
  return (activation_state_ == MM_MODEM_CDMA_ACTIVATION_STATE_NOT_ACTIVATED);
}

bool CellularCapabilityCdma::IsActivated() const {
  return (activation_state_ == MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATED);
}

void CellularCapabilityCdma::OnServiceCreated() {
  SLOG(this, 2) << __func__;
  cellular()->service()->SetActivationType(
      CellularService::kActivationTypeOTASP);
  UpdateServiceActivationStateProperty();
  HandleNewActivationStatus(MM_CDMA_ACTIVATION_ERROR_NONE);
  UpdatePendingActivationState();
}

void CellularCapabilityCdma::UpdateServiceActivationStateProperty() {
  string activation_state;
  if (IsActivating())
    activation_state = kActivationStateActivating;
  else if (IsServiceActivationRequired())
    activation_state = kActivationStateNotActivated;
  else
    activation_state = kActivationStateActivated;
  cellular()->service()->SetActivationState(activation_state);
}

void CellularCapabilityCdma::UpdateServiceOLP() {
  SLOG(this, 2) << __func__;

  // In this case, the Home Provider is trivial. All information comes from the
  // Serving Operator.
  if (!cellular()->serving_operator_info()->IsMobileNetworkOperatorKnown()) {
    return;
  }

  const vector<MobileOperatorInfo::OnlinePortal>& olp_list =
      cellular()->serving_operator_info()->olp_list();
  if (olp_list.empty()) {
    return;
  }

  if (olp_list.size() > 1) {
    SLOG(this, 1) << "Found multiple online portals. Choosing the first.";
  }
  string post_data = olp_list[0].post_data;
  base::ReplaceSubstringsAfterOffset(&post_data, 0, "${esn}",
                                     cellular()->esn());
  base::ReplaceSubstringsAfterOffset(
      &post_data, 0, "${mdn}",
      GetMdnForOLP(cellular()->serving_operator_info()));
  base::ReplaceSubstringsAfterOffset(&post_data, 0, "${meid}",
                                     cellular()->meid());
  base::ReplaceSubstringsAfterOffset(&post_data, 0, "${oem}", "GOG2");
  cellular()->service()->SetOLP(olp_list[0].url, olp_list[0].method, post_data);
}

void CellularCapabilityCdma::GetProperties() {
  SLOG(this, 2) << __func__;
  CellularCapability3gpp::GetProperties();

  std::unique_ptr<DBusPropertiesProxyInterface> properties_proxy =
      control_interface()->CreateDBusPropertiesProxy(
          cellular()->dbus_path(), cellular()->dbus_service());

  KeyValueStore properties(
      properties_proxy->GetAll(MM_DBUS_INTERFACE_MODEM_MODEMCDMA));
  OnModemCdmaPropertiesChanged(properties, vector<string>());
}

void CellularCapabilityCdma::OnActivationStateChangedSignal(
    uint32_t activation_state,
    uint32_t activation_error,
    const KeyValueStore& status_changes) {
  SLOG(this, 2) << __func__;

  activation_state_ = static_cast<MMModemCdmaActivationState>(activation_state);

  string value;
  if (status_changes.ContainsString("mdn"))
    cellular()->set_mdn(status_changes.GetString("mdn"));
  if (status_changes.ContainsString("min"))
    cellular()->set_min(status_changes.GetString("min"));
  SLOG(this, 2) << "Activation state: "
                << GetActivationStateString(activation_state_);

  HandleNewActivationStatus(activation_error);
  UpdatePendingActivationState();
}

void CellularCapabilityCdma::OnActivateReply(const ResultCallback& callback,
                                             const Error& error) {
  SLOG(this, 2) << __func__;
  if (error.IsSuccess()) {
    LOG(INFO) << "Activation completed successfully.";
    modem_info()->pending_activation_store()->SetActivationState(
        PendingActivationStore::kIdentifierMEID, cellular()->meid(),
        PendingActivationStore::kStateActivated);
  } else {
    LOG(ERROR) << "Activation failed with error: " << error;
    modem_info()->pending_activation_store()->SetActivationState(
        PendingActivationStore::kIdentifierMEID, cellular()->meid(),
        PendingActivationStore::kStateFailureRetry);
  }
  UpdatePendingActivationState();

  // CellularCapabilityCdma::ActivateAutomatic passes a dummy
  // ResultCallback when it calls Activate on the proxy object, in which case
  // |callback.is_null()| will return true.
  if (!callback.is_null())
    callback.Run(error);
}

void CellularCapabilityCdma::HandleNewActivationStatus(uint32_t error) {
  SLOG(this, 2) << __func__ << "(" << error << ")";
  if (!cellular()->service().get()) {
    LOG(ERROR) << "In " << __func__ << "(): service is null.";
    return;
  }
  SLOG(this, 2) << "Activation State: " << activation_state_;
  cellular()->service()->SetActivationState(
      GetActivationStateString(activation_state_));
  cellular()->service()->set_error(GetActivationErrorString(error));
  UpdateServiceOLP();
}

void CellularCapabilityCdma::RegisterOnNetwork(const string& network_id,
                                               Error* error,
                                               const ResultCallback& callback) {
  OnUnsupportedOperation(__func__, error);
}

bool CellularCapabilityCdma::IsActivating() const {
  PendingActivationStore::State state =
      modem_info()->pending_activation_store()->GetActivationState(
          PendingActivationStore::kIdentifierMEID, cellular()->meid());
  return (state == PendingActivationStore::kStatePending) ||
         (state == PendingActivationStore::kStateFailureRetry) ||
         (activation_state_ == MM_MODEM_CDMA_ACTIVATION_STATE_ACTIVATING);
}

bool CellularCapabilityCdma::IsRegistered() const {
  return (cdma_1x_registration_state_ !=
              MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN ||
          cdma_evdo_registration_state_ !=
              MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN);
}

void CellularCapabilityCdma::SetUnregistered(bool /*searching*/) {
  cdma_1x_registration_state_ = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
  cdma_evdo_registration_state_ = MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN;
}

void CellularCapabilityCdma::SetupConnectProperties(KeyValueStore* properties) {
  // Skip CellularCapability3gpp::SetupConnectProperties() as it isn't
  // appropriate for CellularCapabilityCdma.
  // TODO(armansito): Remove once 3GPP is implemented in its own class.
}

void CellularCapabilityCdma::RequirePIN(const string& pin,
                                        bool require,
                                        Error* error,
                                        const ResultCallback& callback) {
  OnUnsupportedOperation(__func__, error);
}

void CellularCapabilityCdma::EnterPIN(const string& pin,
                                      Error* error,
                                      const ResultCallback& callback) {
  OnUnsupportedOperation(__func__, error);
}

void CellularCapabilityCdma::UnblockPIN(const string& unblock_code,
                                        const string& pin,
                                        Error* error,
                                        const ResultCallback& callback) {
  OnUnsupportedOperation(__func__, error);
}

void CellularCapabilityCdma::ChangePIN(const string& old_pin,
                                       const string& new_pin,
                                       Error* error,
                                       const ResultCallback& callback) {
  OnUnsupportedOperation(__func__, error);
}

void CellularCapabilityCdma::Reset(Error* error,
                                   const ResultCallback& callback) {
  OnUnsupportedOperation(__func__, error);
}

void CellularCapabilityCdma::Scan(Error* error,
                                  const ResultStringmapsCallback& callback) {
  OnUnsupportedOperation(__func__, error);
}

void CellularCapabilityCdma::OnSimPathChanged(const RpcIdentifier& sim_path) {
  // TODO(armansito): Remove once 3GPP is implemented in its own class.
}

string CellularCapabilityCdma::GetRoamingStateString() const {
  uint32_t state = cdma_evdo_registration_state_;
  if (state == MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN) {
    state = cdma_1x_registration_state_;
  }
  switch (state) {
    case MM_MODEM_CDMA_REGISTRATION_STATE_UNKNOWN:
    case MM_MODEM_CDMA_REGISTRATION_STATE_REGISTERED:
      break;
    case MM_MODEM_CDMA_REGISTRATION_STATE_HOME:
      return kRoamingStateHome;
    case MM_MODEM_CDMA_REGISTRATION_STATE_ROAMING:
      return kRoamingStateRoaming;
    default:
      NOTREACHED();
  }
  return kRoamingStateUnknown;
}

void CellularCapabilityCdma::OnPropertiesChanged(
    const string& interface,
    const KeyValueStore& changed_properties,
    const vector<string>& invalidated_properties) {
  SLOG(this, 2) << __func__ << "(" << interface << ")";
  if (interface == MM_DBUS_INTERFACE_MODEM_MODEMCDMA) {
    OnModemCdmaPropertiesChanged(changed_properties, invalidated_properties);
  } else {
    CellularCapability3gpp::OnPropertiesChanged(interface, changed_properties,
                                                invalidated_properties);
  }
}

void CellularCapabilityCdma::OnModemCdmaPropertiesChanged(
    const KeyValueStore& properties,
    const std::vector<std::string>& /*invalidated_properties*/) {
  SLOG(this, 2) << __func__;
  string str_value;
  if (properties.ContainsString(MM_MODEM_MODEMCDMA_PROPERTY_MEID)) {
    cellular()->set_meid(
        properties.GetString(MM_MODEM_MODEMCDMA_PROPERTY_MEID));
  }
  if (properties.ContainsString(MM_MODEM_MODEMCDMA_PROPERTY_ESN)) {
    cellular()->set_esn(properties.GetString(MM_MODEM_MODEMCDMA_PROPERTY_ESN));
  }

  uint32_t sid = sid_;
  uint32_t nid = nid_;
  MMModemCdmaRegistrationState state_1x = cdma_1x_registration_state_;
  MMModemCdmaRegistrationState state_evdo = cdma_evdo_registration_state_;
  bool registration_changed = false;
  if (properties.ContainsUint(
          MM_MODEM_MODEMCDMA_PROPERTY_CDMA1XREGISTRATIONSTATE)) {
    state_1x = static_cast<MMModemCdmaRegistrationState>(properties.GetUint(
        MM_MODEM_MODEMCDMA_PROPERTY_CDMA1XREGISTRATIONSTATE));
    registration_changed = true;
  }
  if (properties.ContainsUint(
          MM_MODEM_MODEMCDMA_PROPERTY_EVDOREGISTRATIONSTATE)) {
    state_evdo = static_cast<MMModemCdmaRegistrationState>(
        properties.GetUint(MM_MODEM_MODEMCDMA_PROPERTY_EVDOREGISTRATIONSTATE));
    registration_changed = true;
  }
  if (properties.ContainsUint(MM_MODEM_MODEMCDMA_PROPERTY_SID)) {
    sid = properties.GetUint(MM_MODEM_MODEMCDMA_PROPERTY_SID);
    registration_changed = true;
  }
  if (properties.ContainsUint(MM_MODEM_MODEMCDMA_PROPERTY_NID)) {
    nid = properties.GetUint(MM_MODEM_MODEMCDMA_PROPERTY_NID);
    registration_changed = true;
  }
  if (properties.ContainsUint(MM_MODEM_MODEMCDMA_PROPERTY_ACTIVATIONSTATE)) {
    activation_state_ = static_cast<MMModemCdmaActivationState>(
        properties.GetUint(MM_MODEM_MODEMCDMA_PROPERTY_ACTIVATIONSTATE));
    HandleNewActivationStatus(MM_CDMA_ACTIVATION_ERROR_NONE);
  }
  if (registration_changed)
    OnCdmaRegistrationChanged(state_1x, state_evdo, sid, nid);
}

void CellularCapabilityCdma::OnCdmaRegistrationChanged(
    MMModemCdmaRegistrationState state_1x,
    MMModemCdmaRegistrationState state_evdo,
    uint32_t sid,
    uint32_t nid) {
  SLOG(this, 2) << __func__ << ": state_1x=" << state_1x
                << ", state_evdo=" << state_evdo;
  cdma_1x_registration_state_ = state_1x;
  cdma_evdo_registration_state_ = state_evdo;
  sid_ = sid;
  nid_ = nid;
  cellular()->serving_operator_info()->UpdateSID(base::UintToString(sid));
  cellular()->serving_operator_info()->UpdateNID(base::UintToString(nid));
  cellular()->HandleNewRegistrationState();
}

}  // namespace shill
