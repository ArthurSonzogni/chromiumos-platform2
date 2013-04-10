// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_policy_service.h"

#include <base/file_path.h>
#include <base/file_util.h>
#include <base/logging.h>
#include <base/message_loop_proxy.h>

#include "chromeos/switches/chrome_switches.h"
#include "login_manager/chrome_device_policy.pb.h"
#include "login_manager/device_management_backend.pb.h"
#include "login_manager/key_generator.h"
#include "login_manager/login_metrics.h"
#include "login_manager/nss_util.h"
#include "login_manager/owner_key_loss_mitigator.h"
#include "login_manager/policy_key.h"
#include "login_manager/policy_store.h"

namespace em = enterprise_management;

namespace login_manager {
using google::protobuf::RepeatedPtrField;
using std::string;

// static
const char DevicePolicyService::kPolicyPath[] = "/var/lib/whitelist/policy";
// static
const char DevicePolicyService::kSerialRecoveryFlagFile[] =
    "/var/lib/enterprise_serial_number_recovery";
// static
const char DevicePolicyService::kDevicePolicyType[] = "google/chromeos/device";

DevicePolicyService::~DevicePolicyService() {
}

// static
DevicePolicyService* DevicePolicyService::Create(
    LoginMetrics* metrics,
    PolicyKey* owner_key,
    scoped_ptr<OwnerKeyLossMitigator> mitigator,
    NssUtil* nss,
    const scoped_refptr<base::MessageLoopProxy>& main_loop) {
  return new DevicePolicyService(FilePath(kSerialRecoveryFlagFile),
                                 FilePath(kPolicyPath),
                                 scoped_ptr<PolicyStore>(
                                     new PolicyStore(FilePath(kPolicyPath))),
                                 owner_key,
                                 main_loop,
                                 metrics,
                                 mitigator.Pass(),
                                 nss);
}

bool DevicePolicyService::CheckAndHandleOwnerLogin(
    const std::string& current_user,
    bool* is_owner,
    Error* error) {
  // If the current user is the owner, and isn't whitelisted or set as the owner
  // in the settings blob, then do so.
  bool can_access_key = CurrentUserHasOwnerKey(key()->public_key_der(), error);
  if (can_access_key) {
    if (!StoreOwnerProperties(current_user, error))
      return false;
  }

  // Now, the flip side...if we believe the current user to be the owner based
  // on the user field in policy, and she DOESN'T have the private half of the
  // public key, we must mitigate.
  *is_owner = CurrentUserIsOwner(current_user);
  if (*is_owner && !can_access_key) {
    if (!mitigator_->Mitigate(key()))
      return false;
  }
  return true;
}

bool DevicePolicyService::ValidateAndStoreOwnerKey(
    const std::string& current_user,
    const std::string& buf) {
  std::vector<uint8> pub_key;
  NssUtil::BlobFromBuffer(buf, &pub_key);

  Error error;
  if (!CurrentUserHasOwnerKey(pub_key, &error))
   return false;

  if (mitigator_->Mitigating()) {
    // Mitigating: Depending on whether the public key is still present, either
    // clobber or populate regularly.
    if (!(key()->IsPopulated() ? key()->ClobberCompromisedKey(pub_key)
                               : key()->PopulateFromBuffer(pub_key))) {
      return false;
    }
  } else {
    // Not mitigating, so regular key population should work.
    if (!key()->PopulateFromBuffer(pub_key))
      return false;
    // Clear policy in case we're re-establishing ownership.
    store()->Set(em::PolicyFetchResponse());
  }

  if (StoreOwnerProperties(current_user, &error)) {
    PersistKey();
    PersistPolicy();
  } else {
    LOG(WARNING) << "Could not immediately store owner properties in policy";
  }
  return true;
}

DevicePolicyService::DevicePolicyService(
    const FilePath& serial_recovery_flag_file,
    const FilePath& policy_file,
    scoped_ptr<PolicyStore> policy_store,
    PolicyKey* policy_key,
    const scoped_refptr<base::MessageLoopProxy>& main_loop,
    LoginMetrics* metrics,
    scoped_ptr<OwnerKeyLossMitigator> mitigator,
    NssUtil* nss)
    : PolicyService(policy_store.Pass(), policy_key, main_loop),
      serial_recovery_flag_file_(serial_recovery_flag_file),
      policy_file_(policy_file),
      metrics_(metrics),
      mitigator_(mitigator.Pass()),
      nss_(nss) {
}

bool DevicePolicyService::KeyMissing() {
  return key()->HaveCheckedDisk() && !key()->IsPopulated();
}

bool DevicePolicyService::Mitigating() {
  return mitigator_->Mitigating();
}

bool DevicePolicyService::Initialize() {
  bool key_success = key()->PopulateFromDiskIfPossible();
  if (!key_success)
    LOG(ERROR) << "Failed to load device policy key from disk.";

  bool policy_success = store()->LoadOrCreate();
  if (!policy_success)
    LOG(WARNING) << "Failed to load device policy data, continuing anyway.";

  ReportPolicyFileMetrics(key_success, policy_success);
  UpdateSerialNumberRecoveryFlagFile();
  return key_success;
}

bool DevicePolicyService::Store(const uint8* policy_blob,
                                uint32 len,
                                Completion* completion,
                                int flags) {
  bool result = PolicyService::Store(policy_blob, len, completion, flags);

  if (result) {
    UpdateSerialNumberRecoveryFlagFile();

    // Flush the settings cache, the next read will decode the new settings.
    settings_.reset();
  }

  return result;
}

void DevicePolicyService::ReportPolicyFileMetrics(bool key_success,
                                                  bool policy_success) {
  LoginMetrics::PolicyFilesStatus status;
  if (!key_success) {  // Key load failed.
    status.owner_key_file_state = LoginMetrics::MALFORMED;
  } else {
    if (key()->IsPopulated()) {
      if (nss_->CheckPublicKeyBlob(key()->public_key_der()))
        status.owner_key_file_state = LoginMetrics::GOOD;
      else
        status.owner_key_file_state = LoginMetrics::MALFORMED;
    } else {
      status.owner_key_file_state = LoginMetrics::NOT_PRESENT;
    }
  }

  if (!policy_success) {
    status.policy_file_state = LoginMetrics::MALFORMED;
  } else {
    std::string serialized;
    if (!store()->Get().SerializeToString(&serialized))
      status.policy_file_state = LoginMetrics::MALFORMED;
    else if (serialized == "")
      status.policy_file_state = LoginMetrics::NOT_PRESENT;
    else
      status.policy_file_state = LoginMetrics::GOOD;
  }

  if (store()->DefunctPrefsFilePresent())
    status.defunct_prefs_file_state = LoginMetrics::GOOD;

  metrics_->SendPolicyFilesStatus(status);
}

std::vector<std::string> DevicePolicyService::GetStartUpFlags() {
  std::vector<std::string> policy_args;
  const em::ChromeDeviceSettingsProto& policy = GetSettings();
  if (policy.has_start_up_flags()) {
    const em::StartUpFlagsProto& flags_proto = policy.start_up_flags();
    const RepeatedPtrField<std::string>& flags = flags_proto.flags();
    policy_args.push_back(
        std::string("--").append(chromeos::switches::kPolicySwitchesBegin));
    for (RepeatedPtrField<std::string>::const_iterator it = flags.begin();
         it != flags.end(); ++it) {
      std::string flag(*it);
      // Ignore empty flags.
      if (flag.empty() || flag == "-" || flag == "--")
        continue;
      // Check if the flag doesn't start with proper prefix and add it.
      if (flag.length() <= 1 || flag[0] != '-')
        flag = std::string("--").append(flag);
      policy_args.push_back(flag);
    }
    policy_args.push_back(
        std::string("--").append(chromeos::switches::kPolicySwitchesEnd));
  }
  return policy_args;
}

const em::ChromeDeviceSettingsProto& DevicePolicyService::GetSettings() {
  if (!settings_.get()) {
    settings_.reset(new em::ChromeDeviceSettingsProto());

    em::PolicyData policy_data;
    if (!policy_data.ParseFromString(store()->Get().policy_data()) ||
        !settings_->ParseFromString(policy_data.policy_value())) {
      LOG(ERROR) << "Failed to parse device settings, using empty defaults.";
    }
  }

  return *settings_;
}

bool DevicePolicyService::StoreOwnerProperties(const std::string& current_user,
                                               Error* error) {
  const em::PolicyFetchResponse& policy(store()->Get());
  em::PolicyData poldata;
  if (policy.has_policy_data())
    poldata.ParseFromString(policy.policy_data());
  em::ChromeDeviceSettingsProto polval;
  if (poldata.has_policy_type() &&
      poldata.policy_type() == kDevicePolicyType) {
    if (poldata.has_policy_value())
      polval.ParseFromString(poldata.policy_value());
  } else {
    poldata.set_policy_type(kDevicePolicyType);
  }
  // If there existed some device policy, we've got it now!
  // Update the UserWhitelistProto inside the ChromeDeviceSettingsProto we made.
  em::UserWhitelistProto* whitelist_proto = polval.mutable_user_whitelist();
  bool on_list = false;
  const RepeatedPtrField<string>& whitelist = whitelist_proto->user_whitelist();
  for (RepeatedPtrField<string>::const_iterator it = whitelist.begin();
       it != whitelist.end();
       ++it) {
    if (current_user == *it) {
      on_list = true;
      break;
    }
  }
  if (poldata.has_username() && poldata.username() == current_user &&
      on_list &&
      key()->Equals(policy.new_public_key())) {
    return true;  // No changes are needed.
  }
  if (!on_list) {
    // Add owner to the whitelist and turn off whitelist enforcement if it is
    // currently not explicitly turned on or off.
    whitelist_proto->add_user_whitelist(current_user);
    if (!polval.has_allow_new_users())
      polval.mutable_allow_new_users()->set_allow_new_users(true);
  }
  poldata.set_username(current_user);

  // We have now updated the whitelist and owner setting in |polval|.
  // We need to put it into |poldata|, serialize that, sign it, and
  // write it back.
  poldata.set_policy_value(polval.SerializeAsString());
  std::string new_data = poldata.SerializeAsString();
  std::vector<uint8> sig;
  const uint8* data = reinterpret_cast<const uint8*>(new_data.c_str());
  if (!key() || !key()->Sign(data, new_data.length(), &sig)) {
    const char err_msg[] = "Could not sign policy containing new owner data.";
    if (error) {
      LOG(WARNING) << err_msg;
      error->Set(CHROMEOS_LOGIN_ERROR_ILLEGAL_PUBKEY, err_msg);
    } else {
      LOG(ERROR) << err_msg;
    }
    return false;
  }

  em::PolicyFetchResponse new_policy;
  new_policy.CheckTypeAndMergeFrom(policy);
  new_policy.set_policy_data(new_data);
  new_policy.set_policy_data_signature(
      std::string(reinterpret_cast<const char*>(&sig[0]), sig.size()));
  const std::vector<uint8>& key_der = key()->public_key_der();
  new_policy.set_new_public_key(
      std::string(reinterpret_cast<const char*>(&key_der[0]), key_der.size()));
  store()->Set(new_policy);
  return true;
}

bool DevicePolicyService::CurrentUserHasOwnerKey(const std::vector<uint8>& key,
                                                 Error* error) {
  if (!nss_->MightHaveKeys())
    return false;
  if (!nss_->OpenUserDB()) {
    const char msg[] = "Could not open the current user's NSS database.";
    LOG(ERROR) << msg;
    if (error)
      error->Set(CHROMEOS_LOGIN_ERROR_NO_USER_NSSDB, msg);
    return false;
  }
  if (!nss_->GetPrivateKey(key)) {
    const char msg[] = "Could not verify that public key belongs to the owner.";
    LOG(WARNING) << msg;
    if (error)
      error->Set(CHROMEOS_LOGIN_ERROR_ILLEGAL_PUBKEY, msg);
    return false;
  }
  return true;
}

bool DevicePolicyService::CurrentUserIsOwner(const std::string& current_user) {
  const em::PolicyFetchResponse& policy(store()->Get());
  em::PolicyData poldata;
  if (!policy.has_policy_data())
    return false;
  if (poldata.ParseFromString(policy.policy_data())) {
    return (!poldata.has_request_token() &&
            poldata.has_username() &&
            poldata.username() == current_user);
  }
  return false;
}

void DevicePolicyService::UpdateSerialNumberRecoveryFlagFile() {
  const em::PolicyFetchResponse& policy(store()->Get());
  em::PolicyData policy_data;
  int64 policy_size = 0;
  bool recovery_needed = false;
  if (!file_util::GetFileSize(FilePath(policy_file_), &policy_size) ||
      !policy_size) {
    recovery_needed = true;
  }
  if (policy.has_policy_data() &&
      policy_data.ParseFromString(policy.policy_data()) &&
      !policy_data.request_token().empty() &&
      policy_data.valid_serial_number_missing()) {
    recovery_needed = true;
  }

  // We need to recreate the machine info file if |valid_serial_number_missing|
  // is set to true in the protobuf or if the policy file is missing or empty
  // and we need to re-enroll.
  // TODO(pastarmovj,wad): Only check if file is missing if enterprise enrolled.
  // To check that we need to access the install attributes here.
  // For more info see: http://crosbug.com/31537
  if (recovery_needed) {
    if (file_util::WriteFile(serial_recovery_flag_file_, NULL, 0) != 0) {
      PLOG(WARNING) << "Failed to write "
                    << serial_recovery_flag_file_.value();
    }
  } else {
    if (!file_util::Delete(serial_recovery_flag_file_, false)) {
      PLOG(WARNING) << "Failed to delete "
                    << serial_recovery_flag_file_.value();
    }
  }
}

}  // namespace login_manager
