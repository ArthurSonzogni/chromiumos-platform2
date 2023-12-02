// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/cellular_service.h"

#include <optional>
#include <string_view>
#include <unordered_map>

#include <base/check.h>
#include <base/check_op.h>
#include <base/containers/contains.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/stl_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/dbus/shill/dbus-constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/cellular/apn_list.h"
#include "shill/cellular/cellular.h"
#include "shill/cellular/cellular_consts.h"
#include "shill/data_types.h"
#include "shill/dbus/dbus_control.h"
#include "shill/manager.h"
#include "shill/store/property_accessor.h"
#include "shill/store/store_interface.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kCellular;
static std::string ObjectID(const CellularService* c) {
  return c->log_name();
}
}  // namespace Logging

// statics
const char CellularService::kAutoConnActivating[] = "activating";
const char CellularService::kAutoConnBadPPPCredentials[] =
    "bad PPP credentials";
const char CellularService::kAutoConnNoDevice[] = "no device";
const char CellularService::kAutoConnDeviceDisabled[] = "device disabled";
const char CellularService::kAutoConnNotRegistered[] =
    "cellular not registered";
const char CellularService::kAutoConnOutOfCredits[] = "service out of credits";
const char CellularService::kAutoConnSimUnselected[] = "SIM not selected";
const char CellularService::kAutoConnConnectFailed[] =
    "previous connect failed";
const char CellularService::kAutoConnInhibited[] = "inhibited";
const char CellularService::kStorageAPN[] = "Cellular.APN";
const char CellularService::kStorageIccid[] = "Cellular.Iccid";
const char CellularService::kStorageImsi[] = "Cellular.Imsi";
const char CellularService::kStoragePPPUsername[] = "Cellular.PPP.Username";
const char CellularService::kStoragePPPPassword[] = "Cellular.PPP.Password";
const char CellularService::kStorageSimCardId[] = "Cellular.SimCardId";
const char CellularService::kStorageAllowRoaming[] = "Cellular.AllowRoaming";
const char CellularService::kStorageCustomApnList[] = "Cellular.CustomAPNList";

namespace {

const char kGenericServiceNamePrefix[] = "MobileNetwork";

const char kStorageLastGoodAPN[] = "Cellular.LastGoodAPN";
const char kStorageLastConnectedDefaultAPN[] =
    "Cellular.LastConnectedDefaultAPN";
const char kStorageLastConnectedAttachAPN[] = "Cellular.LastConnectedAttachAPN";

bool GetNonEmptyField(const Stringmap& stringmap,
                      const std::string& fieldname,
                      std::string* value) {
  Stringmap::const_iterator it = stringmap.find(fieldname);
  if (it != stringmap.end() && !it->second.empty()) {
    *value = it->second;
    return true;
  }
  return false;
}

void FetchDetailsFromApnList(const Stringmaps& apn_list, Stringmap* apn_info) {
  DCHECK(apn_info);
  std::string apn;
  for (const Stringmap& list_apn_info : apn_list) {
    if (GetNonEmptyField(list_apn_info, kApnProperty, &apn) &&
        (*apn_info)[kApnProperty] == apn) {
      *apn_info = list_apn_info;
      return;
    }
  }
}

bool LoadApnField(const StoreInterface* storage,
                  const std::string& storage_group,
                  const std::string& keytag,
                  const std::string& apntag,
                  Stringmap* apn_info) {
  std::string value;
  if (storage->GetString(storage_group, keytag + "." + apntag, &value) &&
      !value.empty()) {
    (*apn_info)[apntag] = value;
    return true;
  }
  return false;
}

bool ApnFieldExists(const StoreInterface* storage,
                    const std::string& storage_group,
                    const std::string& keytag,
                    const std::string& apntag) {
  return storage->GetString(storage_group, keytag + "." + apntag, NULL);
}

void LoadApn(const StoreInterface* storage,
             const std::string& storage_group,
             const std::string& keytag,
             const Stringmaps& apn_list,
             Stringmap* apn_info) {
  if (keytag == kStorageLastGoodAPN) {
    // Ignore LastGoodAPN that is too old.
    int version;
    if (!LoadApnField(storage, storage_group, keytag,
                      cellular::kApnVersionProperty, apn_info) ||
        !base::StringToInt((*apn_info)[cellular::kApnVersionProperty],
                           &version) ||
        version < cellular::kCurrentApnCacheVersion) {
      if (ApnFieldExists(storage, storage_group, keytag, kApnProperty)) {
        LOG(INFO) << __func__ << ": APN version mismatch: " << keytag;
      }
      return;
    }
  }
  if (!ApnFieldExists(storage, storage_group, keytag, kApnProperty)) {
    LOG(INFO) << __func__
              << ": APN field not previously stored in cache: " << keytag;
    return;
  }
  if (!LoadApnField(storage, storage_group, keytag, kApnProperty, apn_info)) {
    LOG(ERROR) << __func__ << ": Failed to load APN field: " << keytag;
    return;
  }
  if (keytag == CellularService::kStorageAPN)
    FetchDetailsFromApnList(apn_list, apn_info);
  LoadApnField(storage, storage_group, keytag, kApnUsernameProperty, apn_info);
  LoadApnField(storage, storage_group, keytag, kApnPasswordProperty, apn_info);
  LoadApnField(storage, storage_group, keytag, kApnAuthenticationProperty,
               apn_info);
  LoadApnField(storage, storage_group, keytag, kApnIpTypeProperty, apn_info);
  LoadApnField(storage, storage_group, keytag, kApnTypesProperty, apn_info);
  // b/251512775: kApnAttachProperty used to be used to indicate that an APN
  // was an Attach APN. That property was replaced by |kApnTypesProperty| in
  // 2022Q4, but shill needs to migrate the old property into kApnTypesProperty
  // for devices updating from old OS versions.
  if (!base::Contains(*apn_info, kApnTypesProperty)) {
    LoadApnField(storage, storage_group, keytag, kApnAttachProperty, apn_info);
    if (base::Contains(*apn_info, kApnAttachProperty)) {
      (*apn_info)[kApnTypesProperty] =
          ApnList::JoinApnTypes({kApnTypeDefault, kApnTypeIA});
      apn_info->erase(kApnAttachProperty);
    } else {
      (*apn_info)[kApnTypesProperty] = ApnList::JoinApnTypes({kApnTypeDefault});
    }
  }
  // TODO(b/251512775): Chrome still uses the "attach" property in ONC. The
  // reason why kApnAttachProperty is deleted a few lines before, just to be
  // added again, is to keep the migration logic separate from the ONC issue.
  // The ONC might be updated before the old UI is obsoleted.
  if (ApnList::IsAttachApn(*apn_info))
    (*apn_info)[kApnAttachProperty] = kApnAttachProperty;

  LoadApnField(storage, storage_group, keytag, cellular::kApnVersionProperty,
               apn_info);
}

void SaveApnField(StoreInterface* storage,
                  const std::string& storage_group,
                  const Stringmap* apn_info,
                  const std::string& keytag,
                  const std::string& apntag) {
  const std::string key = keytag + "." + apntag;
  std::string str;
  if (apn_info && GetNonEmptyField(*apn_info, apntag, &str))
    storage->SetString(storage_group, key, str);
  else
    storage->DeleteKey(storage_group, key);
}

void SaveApn(StoreInterface* storage,
             const std::string& storage_group,
             const Stringmap* apn_info,
             const std::string& keytag) {
  SaveApnField(storage, storage_group, apn_info, keytag, kApnProperty);
  SaveApnField(storage, storage_group, apn_info, keytag, kApnUsernameProperty);
  SaveApnField(storage, storage_group, apn_info, keytag, kApnPasswordProperty);
  SaveApnField(storage, storage_group, apn_info, keytag,
               kApnAuthenticationProperty);
  SaveApnField(storage, storage_group, apn_info, keytag, kApnIpTypeProperty);
  SaveApnField(storage, storage_group, apn_info, keytag, kApnTypesProperty);
  SaveApnField(storage, storage_group, apn_info, keytag,
               cellular::kApnVersionProperty);
}

}  // namespace

CellularService::CellularService(Manager* manager,
                                 const std::string& imsi,
                                 const std::string& iccid,
                                 const std::string& eid)
    : Service(manager, Technology::kCellular),
      imsi_(imsi),
      iccid_(iccid),
      eid_(eid) {
  // Note: This will change once SetNetworkTechnology() is called, but the
  // serial number remains unchanged so correlating log lines will be easy.
  log_name_ = "cellular_" + base::NumberToString(serial_number());

  // This will get overwritten in Load and in Cellular::UpdateServingOperator
  // when the service is the primary service for the device.
  friendly_name_ =
      kGenericServiceNamePrefix + base::NumberToString(serial_number());

  PropertyStore* store = mutable_store();
  HelpRegisterDerivedString(kActivationTypeProperty,
                            &CellularService::CalculateActivationType, nullptr);
  store->RegisterConstString(kActivationStateProperty, &activation_state_);
  HelpRegisterDerivedStringmap(kCellularApnProperty, &CellularService::GetApn,
                               &CellularService::SetApn);
  HelpRegisterDerivedStringmaps(
      kCellularCustomApnListProperty, &CellularService::GetCustomApnList,
      &CellularService::SetCustomApnList, &CellularService::ClearCustomApnList);
  store->RegisterConstString(kIccidProperty, &iccid_);
  store->RegisterConstString(kImsiProperty, &imsi_);
  store->RegisterConstString(kEidProperty, &eid_);
  store->RegisterConstStringmap(kCellularLastGoodApnProperty,
                                &last_good_apn_info_);
  store->RegisterConstStringmap(kCellularLastAttachApnProperty,
                                &last_attach_apn_info_);
  store->RegisterConstStringmap(kCellularLastConnectedDefaultApnProperty,
                                &last_connected_default_apn_info_);
  store->RegisterConstStringmap(kCellularLastConnectedAttachApnProperty,
                                &last_connected_attach_apn_info_);
  store->RegisterConstString(kNetworkTechnologyProperty, &network_technology_);
  HelpRegisterDerivedBool(kOutOfCreditsProperty,
                          &CellularService::IsOutOfCredits, nullptr);
  store->RegisterConstStringmap(kPaymentPortalProperty, &olp_);
  store->RegisterConstString(kRoamingStateProperty, &roaming_state_);
  store->RegisterConstStringmap(kServingOperatorProperty, &serving_operator_);
  store->RegisterConstString(kUsageURLProperty, &usage_url_);
  store->RegisterString(kCellularPPPUsernameProperty, &ppp_username_);
  store->RegisterWriteOnlyString(kCellularPPPPasswordProperty, &ppp_password_);
  HelpRegisterDerivedBool(kCellularAllowRoamingProperty,
                          &CellularService::GetAllowRoaming,
                          &CellularService::SetAllowRoaming);
  storage_identifier_ = GetDefaultStorageIdentifier();
  SLOG(this, 1) << "CellularService Created: " << log_name();
}

CellularService::~CellularService() {
  SLOG(this, 1) << "CellularService Destroyed: " << log_name();
}

void CellularService::SetDevice(Cellular* device) {
  SLOG(this, 1) << __func__ << ": " << log_name()
                << " Device ICCID: " << (device ? device->iccid() : "None");
  cellular_ = device;
  Error ignored_error;
  adaptor()->EmitRpcIdentifierChanged(kDeviceProperty,
                                      GetDeviceRpcId(&ignored_error));
  adaptor()->EmitBoolChanged(kVisibleProperty,
                             GetVisibleProperty(&ignored_error));
  if (!cellular_) {
    // Do not destroy the service here, Modem may be Inhibited or have reset.
    // If it comes back, the appropriate services will be updated, created, or
    // destroyed from the available SIM properties.
    SetConnectable(false);
    SetState(kStateIdle);
    SetStrength(0);
    return;
  }

  SetConnectable(cellular_->GetConnectable(this));
  SetActivationType(kActivationTypeUnknown);
  if (cellular_->iccid() != iccid_) {
    SetState(kStateIdle);
    SetStrength(0);
  }
}

void CellularService::CompleteCellularActivation(Error* error) {
  if (!cellular_ || cellular_->service() != this) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kOperationFailed,
        base::StringPrintf("CompleteCellularActivation attempted but %s "
                           "Service %s is not active.",
                           kTypeCellular, log_name().c_str()));
    return;
  }
  cellular_->CompleteActivation(error);
}

std::string CellularService::GetStorageIdentifier() const {
  return storage_identifier_;
}

std::string CellularService::GetLoadableStorageIdentifier(
    const StoreInterface& storage) const {
  std::set<std::string> groups =
      storage.GetGroupsWithProperties(GetStorageProperties());
  if (groups.empty()) {
    LOG(WARNING) << "Configuration for service " << log_name()
                 << " is not available in the persistent store";
    return std::string();
  }
  if (groups.size() == 1)
    return *groups.begin();

  // If there are multiple candidates, find the best matching entry. This may
  // happen when loading older profiles.
  LOG(WARNING) << "More than one configuration for service " << log_name()
               << " is available, using the best match and removing others.";

  // If the storage identifier matches, always use that.
  auto iter = std::find(groups.begin(), groups.end(), storage_identifier_);
  if (iter != groups.end())
    return *iter;

  // If an entry with a non-empty IMSI exists, use that.
  for (const std::string& group : groups) {
    std::string imsi;
    storage.GetString(group, kStorageImsi, &imsi);
    if (!imsi.empty())
      return group;
  }
  // Otherwise use the first entry.
  return *groups.begin();
}

bool CellularService::IsLoadableFrom(const StoreInterface& storage) const {
  return !GetLoadableStorageIdentifier(storage).empty();
}

bool CellularService::Load(const StoreInterface* storage) {
  std::string id = GetLoadableStorageIdentifier(*storage);
  if (id.empty()) {
    LOG(WARNING) << "No service with matching properties found";
    return false;
  }

  SLOG(this, 2) << __func__
                << ": Service with matching properties found: " << id;

  std::string default_storage_identifier = storage_identifier_;

  // Set |storage identifier_| to match the storage name in the Profile.
  // This needs to be done before calling Service::Load().
  // NOTE: Older profiles used other identifiers instead of ICCID. This is fine
  // since entries are identified by their properties, not the id.
  storage_identifier_ = id;

  // Load properties common to all Services.
  if (!Service::Load(storage)) {
    // Restore the default storage id. The invalid profile entry will become
    // ignored.
    storage_identifier_ = default_storage_identifier;
    return false;
  }

  // |iccid_| will always match the storage entry.
  // |eid_| is set on construction from the SIM properties.
  storage->GetString(id, kStorageImsi, &imsi_);

  // kStorageName is saved in Service but not loaded. Load the name here, but
  // only set |friendly_name_| if it is not a default name to ensure uniqueness.
  std::string friendly_name;
  if (storage->GetString(id, kStorageName, &friendly_name) &&
      !friendly_name.empty() &&
      !base::StartsWith(friendly_name, kGenericServiceNamePrefix)) {
    friendly_name_ = friendly_name;
  }

  const Stringmaps& apn_list = cellular_ ? cellular_->apn_list() : Stringmaps();
  LoadApn(storage, id, kStorageAPN, apn_list, &apn_info_);
  LoadApn(storage, id, kStorageLastGoodAPN, apn_list, &last_good_apn_info_);
  LoadApn(storage, id, kStorageLastConnectedDefaultAPN, apn_list,
          &last_connected_default_apn_info_);
  LoadApn(storage, id, kStorageLastConnectedAttachAPN, apn_list,
          &last_connected_attach_apn_info_);
  Stringmaps custom_apn_list;
  if (storage->GetStringmaps(id, kStorageCustomApnList, &custom_apn_list))
    custom_apn_list_ = std::move(custom_apn_list);

  const std::string old_username = ppp_username_;
  const std::string old_password = ppp_password_;
  storage->GetString(id, kStoragePPPUsername, &ppp_username_);
  storage->GetString(id, kStoragePPPPassword, &ppp_password_);
  if (IsFailed() && failure() == kFailurePPPAuth &&
      (old_username != ppp_username_ || old_password != ppp_password_)) {
    SetState(kStateIdle);
  }

  storage->GetBool(id, kStorageAllowRoaming, &allow_roaming_);

  return true;
}

bool CellularService::Save(StoreInterface* storage) {
  SLOG(this, 2) << __func__;
  // Save properties common to all Services.
  if (!Service::Save(storage))
    return false;

  const std::string id = GetStorageIdentifier();
  SaveStringOrClear(storage, id, kStorageIccid, iccid_);
  SaveStringOrClear(storage, id, kStorageImsi, imsi_);
  SaveStringOrClear(storage, id, kStorageSimCardId, GetSimCardId());

  SaveApn(storage, id, GetUserSpecifiedApn(), kStorageAPN);
  SaveApn(storage, id, GetLastGoodApn(), kStorageLastGoodAPN);
  SaveApn(storage, id, GetLastConnectedDefaultApn(),
          kStorageLastConnectedDefaultAPN);
  SaveApn(storage, id, GetLastConnectedAttachApn(),
          kStorageLastConnectedAttachAPN);

  if (custom_apn_list_.has_value())
    storage->SetStringmaps(id, kStorageCustomApnList, custom_apn_list_.value());
  else
    storage->DeleteKey(id, kStorageCustomApnList);

  SaveStringOrClear(storage, id, kStoragePPPUsername, ppp_username_);
  SaveStringOrClear(storage, id, kStoragePPPPassword, ppp_password_);

  storage->SetBool(id, kStorageAllowRoaming, allow_roaming_);

  return true;
}

bool CellularService::IsVisible() const {
  return true;
}

const std::string& CellularService::GetSimCardId() const {
  if (!eid_.empty())
    return eid_;
  return iccid_;
}

void CellularService::SetActivationType(ActivationType type) {
  if (type == activation_type_) {
    return;
  }
  activation_type_ = type;
  adaptor()->EmitStringChanged(kActivationTypeProperty,
                               GetActivationTypeString());
}

std::string CellularService::GetActivationTypeString() const {
  switch (activation_type_) {
    case kActivationTypeNonCellular:
      return shill::kActivationTypeNonCellular;
    case kActivationTypeOMADM:
      return shill::kActivationTypeOMADM;
    case kActivationTypeOTA:
      return shill::kActivationTypeOTA;
    case kActivationTypeOTASP:
      return shill::kActivationTypeOTASP;
    case kActivationTypeUnknown:
      return "";
    default:
      NOTREACHED();
      return "";  // Make compiler happy.
  }
}

void CellularService::SetActivationState(const std::string& state) {
  if (state == activation_state_)
    return;

  SLOG(this, 2) << __func__ << ": " << state;

  // If AutoConnect has not been explicitly set by the client, set it to true
  // when the service becomes activated.
  if (!retain_auto_connect() && state == kActivationStateActivated)
    SetAutoConnect(true);

  activation_state_ = state;
  adaptor()->EmitStringChanged(kActivationStateProperty, state);
}

void CellularService::SetOLP(const std::string& url,
                             const std::string& method,
                             const std::string& post_data) {
  Stringmap olp;
  olp[kPaymentPortalURL] = url;
  olp[kPaymentPortalMethod] = method;
  olp[kPaymentPortalPostData] = post_data;

  if (olp_ == olp) {
    return;
  }

  SLOG(this, 2) << __func__ << ": " << url;
  olp_ = olp;
  adaptor()->EmitStringmapChanged(kPaymentPortalProperty, olp);
}

void CellularService::SetUsageURL(const std::string& url) {
  if (url == usage_url_) {
    return;
  }
  usage_url_ = url;
  adaptor()->EmitStringChanged(kUsageURLProperty, url);
}

void CellularService::SetServingOperator(const Stringmap& serving_operator) {
  if (serving_operator_ == serving_operator)
    return;

  serving_operator_ = serving_operator;
  adaptor()->EmitStringmapChanged(kServingOperatorProperty, serving_operator_);
}

void CellularService::SetNetworkTechnology(const std::string& technology) {
  if (technology == network_technology_) {
    return;
  }
  network_technology_ = technology;
  log_name_ = "cellular_" + network_technology_ + "_" +
              base::NumberToString(serial_number());
  adaptor()->EmitStringChanged(kNetworkTechnologyProperty, technology);
}

void CellularService::SetRoamingState(const std::string& state) {
  if (state == roaming_state_) {
    return;
  }
  roaming_state_ = state;
  adaptor()->EmitStringChanged(kRoamingStateProperty, state);
  if (IsRoamingRuleViolated()) {
    Error error;
    OnDisconnect(&error, __func__);
  }
}

bool CellularService::IsRoamingAllowed() {
  if (cellular_ && cellular_->provider_requires_roaming())
    return true;
  return allow_roaming_ && cellular_ && cellular_->policy_allow_roaming();
}

bool CellularService::IsRoamingRuleViolated() {
  if (roaming_state_ != kRoamingStateRoaming)
    return false;

  return !IsRoamingAllowed();
}

Stringmap* CellularService::GetUserSpecifiedApn() {
  Stringmap::iterator it = apn_info_.find(kApnProperty);
  if (it == apn_info_.end() || it->second.empty())
    return nullptr;
  return &apn_info_;
}

Stringmap* CellularService::GetLastGoodApn() {
  Stringmap::iterator it = last_good_apn_info_.find(kApnProperty);
  if (it == last_good_apn_info_.end() || it->second.empty())
    return nullptr;
  return &last_good_apn_info_;
}

void CellularService::SetLastGoodApn(const Stringmap& apn_info) {
  last_good_apn_info_ = apn_info;
  last_connected_default_apn_info_ = apn_info;
  adaptor()->EmitStringmapChanged(kCellularLastGoodApnProperty,
                                  last_good_apn_info_);
  adaptor()->EmitStringmapChanged(kCellularLastConnectedDefaultApnProperty,
                                  last_connected_default_apn_info_);
}

void CellularService::ClearLastGoodApn() {
  last_good_apn_info_.clear();
  adaptor()->EmitStringmapChanged(kCellularLastGoodApnProperty,
                                  last_good_apn_info_);
}

Stringmap* CellularService::GetLastAttachApn() {
  Stringmap::iterator it = last_attach_apn_info_.find(kApnProperty);
  if (it == last_attach_apn_info_.end() || it->second.empty())
    return nullptr;
  return &last_attach_apn_info_;
}

void CellularService::SetLastAttachApn(const Stringmap& apn_info) {
  last_attach_apn_info_ = apn_info;
  adaptor()->EmitStringmapChanged(kCellularLastAttachApnProperty,
                                  last_attach_apn_info_);
}

void CellularService::ClearLastAttachApn() {
  last_attach_apn_info_.clear();
  adaptor()->EmitStringmapChanged(kCellularLastAttachApnProperty,
                                  last_attach_apn_info_);
}

void CellularService::SetLastConnectedAttachApn(const Stringmap& apn_info) {
  last_connected_attach_apn_info_ = apn_info;
  adaptor()->EmitStringmapChanged(kCellularLastConnectedAttachApnProperty,
                                  last_connected_attach_apn_info_);
}

void CellularService::ClearLastConnectedAttachApn() {
  last_connected_attach_apn_info_.clear();
  adaptor()->EmitStringmapChanged(kCellularLastConnectedAttachApnProperty,
                                  last_connected_attach_apn_info_);
}

void CellularService::NotifySubscriptionStateChanged(
    SubscriptionState subscription_state) {
  bool new_out_of_credits =
      (subscription_state == SubscriptionState::kOutOfCredits);
  if (out_of_credits_ == new_out_of_credits)
    return;

  out_of_credits_ = new_out_of_credits;
  SLOG(this, 2) << (out_of_credits_ ? "Marking service out-of-credits"
                                    : "Marking service as not out-of-credits");
  adaptor()->EmitBoolChanged(kOutOfCreditsProperty, out_of_credits_);
}

void CellularService::OnConnect(Error* error) {
  if (!cellular_) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kOperationFailed,
        base::StringPrintf("Connect attempted but %s Service %s has no device.",
                           kTypeCellular, log_name().c_str()));
    return;
  }
  cellular_->Connect(this, error);
}

void CellularService::OnDisconnect(Error* error, const char* reason) {
  if (!cellular_) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kOperationFailed,
        base::StringPrintf(
            "Disconnect attempted but %s Service %s has no device.",
            kTypeCellular, log_name().c_str()));
    return;
  }
  if (cellular_->connect_pending_iccid() == iccid_) {
    cellular_->CancelPendingConnect();
    SetState(kStateIdle);
    return;
  }
  cellular_->Disconnect(error, reason);
}

void CellularService::AutoConnect() {
  const char* reason = nullptr;
  if (!IsAutoConnectable(&reason)) {
    if (reason == kAutoConnTechnologyNotAutoConnectable ||
        reason == kAutoConnConnected) {
      SLOG(this, 3) << "Suppressed autoconnect to " << log_name()
                    << " Reason: " << reason;
    } else if (reason == kAutoConnBusy ||
               reason == kAutoConnMediumUnavailable) {
      SLOG(this, 1) << "Suppressed autoconnect to " << log_name()
                    << " Reason: " << reason;
    } else if (reason == kAutoConnNotRegistered) {
      SLOG(this, 1) << "Skip autoconnect attempt to " << log_name()
                    << " Reason: " << reason;
      ThrottleFutureAutoConnects();
    } else {
      LOG(INFO) << "Suppressed autoconnect to " << log_name()
                << " Reason: " << reason;
    }
    return;
  }

  Error error;
  LOG(INFO) << "Auto-connecting to " << log_name();
  ThrottleFutureAutoConnects();
  Connect(&error, __func__);
}

bool CellularService::IsAutoConnectable(const char** reason) const {
  if (!cellular_) {
    *reason = kAutoConnNoDevice;
    return false;
  }
  if (!cellular_->enabled()) {
    *reason = kAutoConnDeviceDisabled;
    return false;
  }
  if (cellular_->service()) {
    if (cellular_->service()->IsConnected()) {
      *reason = kAutoConnConnected;
      return false;
    }
    if (cellular_->service()->IsConnecting()) {
      *reason = kAutoConnBusy;
      return false;
    }
  }
  if (cellular_->IsActivating()) {
    *reason = kAutoConnActivating;
    return false;
  }

  if (!Service::IsAutoConnectable(reason)) {
    return false;
  }

  if (cellular_->iccid() != iccid()) {
    *reason = kAutoConnSimUnselected;
    return false;
  }
  if (!cellular_->StateIsRegistered()) {
    *reason = kAutoConnNotRegistered;
    return false;
  }
  if (cellular_->inhibited()) {
    *reason = kAutoConnInhibited;
    return false;
  }
  if (!cellular_->connect_pending_iccid().empty()) {
    *reason = kAutoConnConnecting;
    return false;
  }
  if (failure() == kFailurePPPAuth) {
    *reason = kAutoConnBadPPPCredentials;
    return false;
  }
  if (out_of_credits_) {
    *reason = kAutoConnOutOfCredits;
    return false;
  }
  return true;
}

base::TimeDelta CellularService::GetMinAutoConnectCooldownTime() const {
  return base::Seconds(10);
}

base::TimeDelta CellularService::GetMaxAutoConnectCooldownTime() const {
  return base::Minutes(30);
}

bool CellularService::IsDisconnectable(Error* error) const {
  if (!cellular_) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kNotConnected,
        base::StringPrintf("Disconnect attempted with no Cellular Device: %s",
                           log_name().c_str()));
    return false;
  }
  if (cellular_->connect_pending_iccid() == iccid_) {
    // Allow disconnecting when a connect is pending.
    return true;
  }
  return Service::IsDisconnectable(error);
}

bool CellularService::IsMeteredByServiceProperties() const {
  // TODO(crbug.com/989639): see if we can detect unmetered cellular
  // connections automatically.
  return true;
}

RpcIdentifier CellularService::GetDeviceRpcId(Error* error) const {
  // Only provide cellular_->GetRpcIdentifier() if this is the active service.
  if (!cellular_ || iccid() != cellular_->iccid())
    return DBusControl::NullRpcIdentifier();
  return cellular_->GetRpcIdentifier();
}

void CellularService::HelpRegisterDerivedString(
    std::string_view name,
    std::string (CellularService::*get)(Error* error),
    bool (CellularService::*set)(const std::string& value, Error* error)) {
  mutable_store()->RegisterDerivedString(
      name, StringAccessor(new CustomAccessor<CellularService, std::string>(
                this, get, set)));
}

void CellularService::HelpRegisterDerivedStringmap(
    std::string_view name,
    Stringmap (CellularService::*get)(Error* error),
    bool (CellularService::*set)(const Stringmap& value, Error* error)) {
  mutable_store()->RegisterDerivedStringmap(
      name, StringmapAccessor(new CustomAccessor<CellularService, Stringmap>(
                this, get, set)));
}

void CellularService::HelpRegisterDerivedStringmaps(
    std::string_view name,
    Stringmaps (CellularService::*get)(Error* error),
    bool (CellularService::*set)(const Stringmaps& value, Error* error),
    void (CellularService::*clear)(Error*)) {
  mutable_store()->RegisterDerivedStringmaps(
      name, StringmapsAccessor(new CustomAccessor<CellularService, Stringmaps>(
                this, get, set, clear)));
}

void CellularService::HelpRegisterDerivedBool(
    std::string_view name,
    bool (CellularService::*get)(Error* error),
    bool (CellularService::*set)(const bool&, Error*)) {
  mutable_store()->RegisterDerivedBool(
      name,
      BoolAccessor(new CustomAccessor<CellularService, bool>(this, get, set)));
}

std::set<std::string> CellularService::GetStorageGroupsWithProperty(
    const StoreInterface& storage,
    const std::string& key,
    const std::string& value) const {
  KeyValueStore properties;
  properties.Set<std::string>(kStorageType, kTypeCellular);
  properties.Set<std::string>(key, value);
  return storage.GetGroupsWithProperties(properties);
}

std::string CellularService::CalculateActivationType(Error* error) {
  return GetActivationTypeString();
}

Stringmap CellularService::ValidateCustomApn(const Stringmap& value,
                                             bool using_apn_revamp_ui) {
  DCHECK(cellular_);
  // Only copy in the fields we care about, and validate the contents.
  // If the "apn" field is missing or empty, the APN is cleared.
  std::string new_apn;
  Stringmap new_apn_info;
  if (GetNonEmptyField(value, kApnProperty, &new_apn)) {
    new_apn_info[kApnProperty] = new_apn;

    // Fetch details from the APN database first.
    if (!using_apn_revamp_ui) {
      // For the revamp APN UI, it was decided that the user would have full
      // control over the APN, so we should not try to "fix" their APN by
      // populating values from the modb.
      FetchDetailsFromApnList(cellular_->apn_list(), &new_apn_info);
    }
    // If this is a user-entered APN, then one or more of the following
    // details should exist, even if they are empty.
    std::string str;
    if (GetNonEmptyField(value, kApnUsernameProperty, &str))
      new_apn_info[kApnUsernameProperty] = str;
    if (GetNonEmptyField(value, kApnPasswordProperty, &str))
      new_apn_info[kApnPasswordProperty] = str;
    if (GetNonEmptyField(value, kApnAuthenticationProperty, &str))
      new_apn_info[kApnAuthenticationProperty] = str;
    if (using_apn_revamp_ui) {
      if (GetNonEmptyField(value, kApnTypesProperty, &str))
        new_apn_info[kApnTypesProperty] = str;
      if (GetNonEmptyField(value, kApnIdProperty, &str))
        new_apn_info[kApnIdProperty] = str;
      if (GetNonEmptyField(value, kApnSourceProperty, &str))
        new_apn_info[kApnSourceProperty] = str;
      if (GetNonEmptyField(value, kApnIpTypeProperty, &str))
        new_apn_info[kApnIpTypeProperty] = str;
    } else {
      // TODO(b/251512775): Chrome will keep sending the "attach" value on
      // |SetApn| until the old UI is obsoleted. Convert the attach value into
      // |kApnTypesProperty|, and retain |kApnAttachProperty| since it's used
      // by ONC.
      // SetApn should not contain the key |kApnTypesProperty|.
      if (GetNonEmptyField(value, kApnAttachProperty, &str)) {
        new_apn_info[kApnTypesProperty] =
            ApnList::JoinApnTypes({kApnTypeIA, kApnTypeDefault});
        new_apn_info[kApnAttachProperty] = kApnAttachProperty;
      } else if (!base::Contains(new_apn_info, kApnTypesProperty)) {
        // Skip setting |kApnTypesProperty| if the value was populated from the
        // modb.
        new_apn_info[kApnTypesProperty] =
            ApnList::JoinApnTypes({kApnTypeDefault});
      }
    }
    new_apn_info[cellular::kApnVersionProperty] =
        base::NumberToString(cellular::kCurrentApnCacheVersion);
  }
  return new_apn_info;
}

Stringmap CellularService::GetApn(Error* /*error*/) {
  return apn_info_;
}

bool CellularService::SetApn(const Stringmap& value, Error* error) {
  if (!cellular_) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kOperationFailed,
        base::StringPrintf(
            "Failed setting user APN: %s Service %s has no device.",
            kTypeCellular, log_name().c_str()));
    return false;
  }

  Stringmap new_apn_info = ValidateCustomApn(value, false);
  if (apn_info_ == new_apn_info) {
    return true;
  }
  apn_info_ = new_apn_info;
  adaptor()->EmitStringmapChanged(kCellularApnProperty, apn_info_);

  bool configure_attach_apn = ApnList::IsAttachApn(apn_info_) ||
                              ApnList::IsAttachApn(last_attach_apn_info_);
  return CustomApnUpdated(configure_attach_apn, error);
}

bool CellularService::CustomApnUpdated(bool configure_attach_apn,
                                       Error* error) {
  bool is_connected = IsConnected();
  if (is_connected) {
    Disconnect(error, __func__);
    if (!error->IsSuccess()) {
      return false;
    }
  }
  if (configure_attach_apn) {
    // If we were using an attach APN, and we are no longer using it, we should
    // re-configure the attach APN to clear the attach APN in the modem.
    cellular_->ConfigureAttachApn();
    return true;
  }
  if (is_connected) {
    Connect(error, __func__);
    return error->IsSuccess();
  }
  ResetAutoConnectCooldownTime();
  // UpdateService to trigger AutoConnect if necessary.
  manager()->UpdateService(this);
  return true;
}

Stringmap* CellularService::GetLastConnectedDefaultApn() {
  Stringmap::iterator it = last_connected_default_apn_info_.find(kApnProperty);
  if (it == last_connected_default_apn_info_.end() || it->second.empty())
    return nullptr;
  return &last_connected_default_apn_info_;
}

Stringmap* CellularService::GetLastConnectedAttachApn() {
  Stringmap::iterator it = last_connected_attach_apn_info_.find(kApnProperty);
  if (it == last_connected_attach_apn_info_.end() || it->second.empty())
    return nullptr;
  return &last_connected_attach_apn_info_;
}

Stringmaps CellularService::GetCustomApnList(Error* /*error*/) {
  SLOG(this, 2) << __func__;
  return custom_apn_list_.value_or(Stringmaps());
}

bool CellularService::SetCustomApnList(const Stringmaps& value, Error* error) {
  SLOG(this, 2) << __func__;
  bool exist_attach = false;
  Stringmaps new_apn_info_list;

  if (!cellular_) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kOperationFailed,
        base::StringPrintf(
            "Failed setting user APN list: %s Service %s has no device.",
            kTypeCellular, log_name().c_str()));
    return false;
  }

  for (auto& apn_value : value) {
    Stringmap new_apn_info =
        new_apn_info_list.emplace_back(ValidateCustomApn(apn_value, true));
    exist_attach = exist_attach || ApnList::IsAttachApn(new_apn_info);
  }

  if (custom_apn_list_.has_value() &&
      custom_apn_list_.value() == new_apn_info_list) {
    return true;
  }
  custom_apn_list_.emplace(new_apn_info_list);
  adaptor()->EmitStringmapsChanged(kCellularCustomApnListProperty,
                                   custom_apn_list_.value());

  // will be nullptr if it was not set
  Stringmap* last_attach_apn_info = GetLastAttachApn();

  bool configure_attach_apn = exist_attach || !last_attach_apn_info ||
                              ApnList::IsAttachApn(*last_attach_apn_info);
  return CustomApnUpdated(configure_attach_apn, error);
}

void CellularService::ClearCustomApnList(Error* error) {
  SLOG(this, 2) << __func__;
  custom_apn_list_.reset();
  adaptor()->EmitStringmapsChanged(kCellularCustomApnListProperty,
                                   Stringmaps());

  if (!cellular_) {
    Error::PopulateAndLog(
        FROM_HERE, error, Error::kOperationFailed,
        base::StringPrintf(
            "Failed clearing user APN list: %s Service %s has no device.",
            kTypeCellular, log_name().c_str()));
    return;
  }
  CustomApnUpdated(true, error);
}

KeyValueStore CellularService::GetStorageProperties() const {
  KeyValueStore properties;
  properties.Set<std::string>(kStorageType, kTypeCellular);
  properties.Set<std::string>(kStorageIccid, iccid_);
  return properties;
}

std::string CellularService::GetDefaultStorageIdentifier() const {
  if (iccid_.empty()) {
    LOG(ERROR) << "CellularService created with empty ICCID.";
    return std::string();
  }
  return SanitizeStorageIdentifier(
      base::StringPrintf("%s_%s", kTypeCellular, iccid_.c_str()));
}

bool CellularService::IsOutOfCredits(Error* /*error*/) {
  return out_of_credits_;
}

bool CellularService::SetAllowRoaming(const bool& value, Error* error) {
  SLOG(this, 2) << __func__ << ": " << value;
  if (allow_roaming_ == value)
    return false;

  allow_roaming_ = value;
  manager()->UpdateService(this);
  adaptor()->EmitBoolChanged(kCellularAllowRoamingProperty, value);

  if (IsRoamingRuleViolated()) {
    Error disconnect_error;
    OnDisconnect(&disconnect_error, __func__);
  }

  return true;
}

bool CellularService::GetAllowRoaming(Error* /*error*/) {
  return allow_roaming_;
}

}  // namespace shill
