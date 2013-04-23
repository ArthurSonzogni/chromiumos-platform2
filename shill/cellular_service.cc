// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular_service.h"

#include <string>

#include <base/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/cellular.h"
#include "shill/property_accessor.h"
#include "shill/store_interface.h"

using std::string;

namespace shill {

const char CellularService::kAutoConnActivating[] = "activating";
const char CellularService::kAutoConnDeviceDisabled[] = "device disabled";
const char CellularService::kAutoConnOutOfCredits[] = "device out of credits";
const char CellularService::kAutoConnOutOfCreditsDetectionInProgress[] =
    "device detecting out-of-credits";
const int64 CellularService::kOutOfCreditsConnectionDropSeconds = 15;
const int CellularService::kOutOfCreditsMaxConnectAttempts = 3;
const int64 CellularService::kOutOfCreditsResumeIgnoreSeconds = 5;
const char CellularService::kStorageAPN[] = "Cellular.APN";
const char CellularService::kStorageLastGoodAPN[] = "Cellular.LastGoodAPN";

// TODO(petkov): Add these to system_api/dbus/service_constants.h
namespace {
const char kKeyOLPURL[] = "url";
const char kKeyOLPMethod[] = "method";
const char kKeyOLPPostData[] = "postdata";
}  // namespace

static bool GetNonEmptyField(const Stringmap &stringmap,
                             const string &fieldname,
                             string *value) {
  Stringmap::const_iterator it = stringmap.find(fieldname);
  if (it != stringmap.end() && !it->second.empty()) {
    *value = it->second;
    return true;
  }
  return false;
}

CellularService::OLP::OLP() {
  SetURL("");
  SetMethod("");
  SetPostData("");
}

CellularService::OLP::~OLP() {}

void CellularService::OLP::CopyFrom(const OLP &olp) {
  dict_ = olp.dict_;
}

bool CellularService::OLP::Equals(const OLP &olp) const {
  return dict_ == olp.dict_;
}

const string &CellularService::OLP::GetURL() const {
  return dict_.find(kKeyOLPURL)->second;
}

void CellularService::OLP::SetURL(const string &url) {
  dict_[kKeyOLPURL] = url;
}

const string &CellularService::OLP::GetMethod() const {
  return dict_.find(kKeyOLPMethod)->second;
}

void CellularService::OLP::SetMethod(const string &method) {
  dict_[kKeyOLPMethod] = method;
}

const string &CellularService::OLP::GetPostData() const {
  return dict_.find(kKeyOLPPostData)->second;
}

void CellularService::OLP::SetPostData(const string &post_data) {
  dict_[kKeyOLPPostData] = post_data;
}

const Stringmap &CellularService::OLP::ToDict() const {
  return dict_;
}

CellularService::CellularService(ControlInterface *control_interface,
                                 EventDispatcher *dispatcher,
                                 Metrics *metrics,
                                 Manager *manager,
                                 const CellularRefPtr &device)
    : Service(control_interface, dispatcher, metrics, manager,
              Technology::kCellular),
      weak_ptr_factory_(this),
      activate_over_non_cellular_network_(false),
      cellular_(device),
      is_auto_connecting_(false),
      enforce_out_of_credits_detection_(false),
      num_connect_attempts_(0),
      out_of_credits_detection_in_progress_(false),
      out_of_credits_(false) {
  set_connectable(true);
  PropertyStore *store = this->mutable_store();
  store->RegisterConstBool(kActivateOverNonCellularNetworkProperty,
                           &activate_over_non_cellular_network_);
  store->RegisterConstString(flimflam::kActivationStateProperty,
                             &activation_state_);
  HelpRegisterDerivedStringmap(flimflam::kCellularApnProperty,
                               &CellularService::GetApn,
                               &CellularService::SetApn);
  store->RegisterConstStringmap(flimflam::kCellularLastGoodApnProperty,
                                &last_good_apn_info_);
  store->RegisterConstString(flimflam::kNetworkTechnologyProperty,
                             &network_technology_);
  store->RegisterConstBool(kOutOfCreditsProperty, &out_of_credits_);
  store->RegisterConstStringmap(flimflam::kPaymentPortalProperty,
                                &olp_.ToDict());
  store->RegisterConstString(flimflam::kRoamingStateProperty, &roaming_state_);
  store->RegisterConstStringmap(flimflam::kServingOperatorProperty,
                                &serving_operator_.ToDict());
  store->RegisterConstString(flimflam::kUsageURLProperty, &usage_url_);

  string name = device->CreateFriendlyServiceName();
  set_friendly_name(name);
  SetStorageIdentifier(string(flimflam::kTypeCellular) + "_" +
                       device->address() + "_" + name);
}

CellularService::~CellularService() { }

bool CellularService::IsAutoConnectable(const char **reason) const {
  if (!cellular_->running()) {
    *reason = kAutoConnDeviceDisabled;
    return false;
  }
  if (cellular_->IsActivating()) {
    *reason = kAutoConnActivating;
    return false;
  }
  if (out_of_credits_detection_in_progress_) {
    *reason = kAutoConnOutOfCreditsDetectionInProgress;
    return false;
  }
  if (out_of_credits_) {
    *reason = kAutoConnOutOfCredits;
    return false;
  }
  return Service::IsAutoConnectable(reason);
}

void CellularService::HelpRegisterDerivedStringmap(
    const string &name,
    Stringmap(CellularService::*get)(Error *error),
    void(CellularService::*set)(
        const Stringmap &value, Error *error)) {
  mutable_store()->RegisterDerivedStringmap(
      name,
      StringmapAccessor(
          new CustomAccessor<CellularService, Stringmap>(this, get, set)));
}

Stringmap *CellularService::GetUserSpecifiedApn() {
  Stringmap::iterator it = apn_info_.find(flimflam::kApnProperty);
  if (it == apn_info_.end() || it->second.empty())
    return NULL;
  return &apn_info_;
}

Stringmap *CellularService::GetLastGoodApn() {
  Stringmap::iterator it =
      last_good_apn_info_.find(flimflam::kApnProperty);
  if (it == last_good_apn_info_.end() || it->second.empty())
    return NULL;
  return &last_good_apn_info_;
}

Stringmap CellularService::GetApn(Error */*error*/) {
  return apn_info_;
}

void CellularService::SetApn(const Stringmap &value, Error *error) {
  // Only copy in the fields we care about, and validate the contents.
  // If the "apn" field is missing or empty, the APN is cleared.
  string str;
  if (!GetNonEmptyField(value, flimflam::kApnProperty, &str)) {
    apn_info_.clear();
  } else {
    apn_info_[flimflam::kApnProperty] = str;
    if (GetNonEmptyField(value, flimflam::kApnUsernameProperty, &str))
      apn_info_[flimflam::kApnUsernameProperty] = str;
    if (GetNonEmptyField(value, flimflam::kApnPasswordProperty, &str))
      apn_info_[flimflam::kApnPasswordProperty] = str;
    // Clear the last good APN, otherwise the one the user just
    // set won't be used, since LastGoodApn comes first in the
    // search order when trying to connect. Only do this if a
    // non-empty user APN has been supplied. If the user APN is
    // being cleared, leave LastGoodApn alone.
    ClearLastGoodApn();
  }
  adaptor()->EmitStringmapChanged(flimflam::kCellularApnProperty, apn_info_);
  SaveToCurrentProfile();
}

void CellularService::SetLastGoodApn(const Stringmap &apn_info) {
  last_good_apn_info_ = apn_info;
  adaptor()->EmitStringmapChanged(flimflam::kCellularLastGoodApnProperty,
                                  last_good_apn_info_);
  SaveToCurrentProfile();
}

void CellularService::ClearLastGoodApn() {
  last_good_apn_info_.clear();
  adaptor()->EmitStringmapChanged(flimflam::kCellularLastGoodApnProperty,
                                  last_good_apn_info_);
  SaveToCurrentProfile();
}

void CellularService::OnAfterResume() {
  Service::OnAfterResume();
  resume_start_time_ = base::Time::Now();
}

bool CellularService::Load(StoreInterface *storage) {
  // Load properties common to all Services.
  if (!Service::Load(storage))
    return false;

  const string id = GetStorageIdentifier();
  LoadApn(storage, id, kStorageAPN, &apn_info_);
  LoadApn(storage, id, kStorageLastGoodAPN, &last_good_apn_info_);
  return true;
}

void CellularService::LoadApn(StoreInterface *storage,
                              const string &storage_group,
                              const string &keytag,
                              Stringmap *apn_info) {
  if (!LoadApnField(storage, storage_group, keytag,
               flimflam::kApnProperty, apn_info))
    return;
  LoadApnField(storage, storage_group, keytag,
               flimflam::kApnUsernameProperty, apn_info);
  LoadApnField(storage, storage_group, keytag,
               flimflam::kApnPasswordProperty, apn_info);
}

bool CellularService::LoadApnField(StoreInterface *storage,
                                   const string &storage_group,
                                   const string &keytag,
                                   const string &apntag,
                                   Stringmap *apn_info) {
  string value;
  if (storage->GetString(storage_group, keytag + "." + apntag, &value) &&
      !value.empty()) {
    (*apn_info)[apntag] = value;
    return true;
  }
  return false;
}

void CellularService::PerformOutOfCreditsDetection(ConnectState curr_state,
                                                   ConnectState new_state) {
  // WORKAROUND:
  // Some modems on Verizon network does not properly redirect when a SIM
  // runs out of credits.  This workaround is used to detect an out-of-credits
  // condition by by retrying a connect request if it was dropped within
  // kOutOfCreditsConnectionDropSeconds.  If the number of retries exceeds
  // kOutOfCreditsMaxConnectAttempts, then the SIM is considered
  // out-of-credits and the cellular service kOutOfCreditsProperty is set.
  // This will signal Chrome to display the appropriate UX and also suppress
  // auto-connect until the next time the user manually connects.
  //
  // TODO(thieule): Remove this workaround (crosbug.com/p/18169).
  if (out_of_credits_) {
    SLOG(Cellular, 2) << __func__
                      << ": Already out-of-credits, skipping check";
    return;
  }
  base::TimeDelta
      time_since_resume = base::Time::Now() - resume_start_time_;
  if (time_since_resume.InSeconds() < kOutOfCreditsResumeIgnoreSeconds) {
    // On platforms that power down the modem during suspend, make sure that
    // we do not display a false out-of-credits warning to the user
    // due to the sequence below by skipping out-of-credits detection
    // immediately after a resume.
    //   1. User suspends Chromebook.
    //   2. Hardware turns off power to modem.
    //   3. User resumes Chromebook.
    //   4. Hardware restores power to modem.
    //   5. ModemManager still has instance of old modem.
    //      ModemManager does not delete this instance until udev fires a
    //      device removed event.  ModemManager does not detect new modem
    //      until udev fires a new device event.
    //   6. Shill performs auto-connect against the old modem.
    //      Make sure at this step that we do not display a false
    //      out-of-credits warning.
    //   7. Udev fires device removed event.
    //   8. Udev fires new device event.
    SLOG(Cellular, 2) <<
        "Skipping out-of-credits detection, too soon since resume.";
    ResetOutOfCreditsState();
    return;
  }
  base::TimeDelta
      time_since_connect = base::Time::Now() - connect_start_time_;
  if (time_since_connect.InSeconds() > kOutOfCreditsConnectionDropSeconds) {
    ResetOutOfCreditsState();
    return;
  }
  // Verizon can drop the connection in two ways:
  //   - Denies the connect request
  //   - Allows connect request but disconnects later
  bool connection_dropped =
      (IsConnectedState(curr_state) || IsConnectingState(curr_state)) &&
      (new_state == kStateFailure || new_state == kStateIdle);
  if (!connection_dropped)
    return;
  if (explicitly_disconnected())
    return;
  if (roaming_state_ == flimflam::kRoamingStateRoaming &&
      !cellular_->allow_roaming_property())
    return;
  if (time_since_connect.InSeconds() <= kOutOfCreditsConnectionDropSeconds) {
    if (num_connect_attempts_ < kOutOfCreditsMaxConnectAttempts) {
      SLOG(Cellular, 2) << "Out-Of-Credits detection: Reconnecting "
                        << "(retry #" << num_connect_attempts_ << ")";
      // Prevent autoconnect logic from kicking in while we perform the
      // out-of-credits detection.
      out_of_credits_detection_in_progress_ = true;
      dispatcher()->PostTask(
          Bind(&CellularService::OutOfCreditsReconnect,
               weak_ptr_factory_.GetWeakPtr()));
    } else {
      LOG(ERROR) <<
          "Out-Of-Credits detection: Marking service as out-of-credits";
      metrics()->NotifyCellularOutOfCredits(
          Metrics::kCellularOutOfCreditsReasonConnectDisconnectLoop);
      SetOutOfCredits(true);
      ResetOutOfCreditsState();
    }
  }
}

void CellularService::OutOfCreditsReconnect() {
  Error error;
  Connect(&error, __func__);
}

void CellularService::ResetOutOfCreditsState() {
  out_of_credits_detection_in_progress_ = false;
  num_connect_attempts_ = 0;
}

bool CellularService::Save(StoreInterface *storage) {
  // Save properties common to all Services.
  if (!Service::Save(storage))
    return false;

  const string id = GetStorageIdentifier();
  SaveApn(storage, id, GetUserSpecifiedApn(), kStorageAPN);
  SaveApn(storage, id, GetLastGoodApn(), kStorageLastGoodAPN);
  return true;
}

void CellularService::SaveApn(StoreInterface *storage,
                              const string &storage_group,
                              const Stringmap *apn_info,
                              const string &keytag) {
    SaveApnField(storage, storage_group, apn_info, keytag,
                 flimflam::kApnProperty);
    SaveApnField(storage, storage_group, apn_info, keytag,
                 flimflam::kApnUsernameProperty);
    SaveApnField(storage, storage_group, apn_info, keytag,
                 flimflam::kApnPasswordProperty);
}

void CellularService::SaveApnField(StoreInterface *storage,
                                   const string &storage_group,
                                   const Stringmap *apn_info,
                                   const string &keytag,
                                   const string &apntag) {
  const string key = keytag + "." + apntag;
  string str;
  if (apn_info && GetNonEmptyField(*apn_info, apntag, &str))
    storage->SetString(storage_group, key, str);
  else
    storage->DeleteKey(storage_group, key);
}

void CellularService::AutoConnect() {
  is_auto_connecting_ = true;
  Service::AutoConnect();
  is_auto_connecting_ = false;
}

void CellularService::Connect(Error *error, const char *reason) {
  if (num_connect_attempts_ == 0)
    SetOutOfCredits(false);
  connect_start_time_ = base::Time::Now();
  num_connect_attempts_++;
  Service::Connect(error, reason);
  cellular_->Connect(error);
  if (error->IsFailure())
    ResetOutOfCreditsState();
}

void CellularService::Disconnect(Error *error) {
  Service::Disconnect(error);
  cellular_->Disconnect(error);
}

void CellularService::ActivateCellularModem(const string &carrier,
                                            Error *error,
                                            const ResultCallback &callback) {
  cellular_->Activate(carrier, error, callback);
}

void CellularService::CompleteCellularActivation(Error *error) {
  cellular_->CompleteActivation(error);
}

void CellularService::SetState(ConnectState new_state) {
  if (enforce_out_of_credits_detection_)
    PerformOutOfCreditsDetection(state(), new_state);
  Service::SetState(new_state);
}

void CellularService::SetStorageIdentifier(const string &identifier) {
  storage_identifier_ = identifier;
  std::replace_if(storage_identifier_.begin(),
                  storage_identifier_.end(),
                  &Service::IllegalChar, '_');
}

string CellularService::GetStorageIdentifier() const {
  return storage_identifier_;
}

string CellularService::GetDeviceRpcId(Error */*error*/) {
  return cellular_->GetRpcIdentifier();
}

void CellularService::SetActivateOverNonCellularNetwork(bool state) {
  if (state == activate_over_non_cellular_network_) {
    return;
  }
  activate_over_non_cellular_network_ = state;
  adaptor()->EmitBoolChanged(kActivateOverNonCellularNetworkProperty, state);
}

void CellularService::SetActivationState(const string &state) {
  if (state == activation_state_) {
    return;
  }
  activation_state_ = state;
  adaptor()->EmitStringChanged(flimflam::kActivationStateProperty, state);
  SetConnectable(state != flimflam::kActivationStateNotActivated);
}

void CellularService::SetOLP(const OLP &olp) {
  if (olp_.Equals(olp)) {
    return;
  }
  olp_.CopyFrom(olp);
  adaptor()->EmitStringmapChanged(flimflam::kPaymentPortalProperty,
                                  olp.ToDict());
}

void CellularService::SetUsageURL(const string &url) {
  if (url == usage_url_) {
    return;
  }
  usage_url_ = url;
  adaptor()->EmitStringChanged(flimflam::kUsageURLProperty, url);
}

void CellularService::SetNetworkTechnology(const string &technology) {
  if (technology == network_technology_) {
    return;
  }
  network_technology_ = technology;
  adaptor()->EmitStringChanged(flimflam::kNetworkTechnologyProperty,
                               technology);
}

void CellularService::SetRoamingState(const string &state) {
  if (state == roaming_state_) {
    return;
  }
  roaming_state_ = state;
  adaptor()->EmitStringChanged(flimflam::kRoamingStateProperty, state);
}

void CellularService::SetOutOfCredits(bool state) {
  if (state == out_of_credits_) {
    return;
  }
  out_of_credits_ = state;
  adaptor()->EmitBoolChanged(kOutOfCreditsProperty, state);
}

const Cellular::Operator &CellularService::serving_operator() const {
  return serving_operator_;
}

void CellularService::SetServingOperator(const Cellular::Operator &oper) {
  if (serving_operator_.Equals(oper)) {
    return;
  }
  serving_operator_.CopyFrom(oper);
  adaptor()->EmitStringmapChanged(flimflam::kServingOperatorProperty,
                                  oper.ToDict());
}

}  // namespace shill
