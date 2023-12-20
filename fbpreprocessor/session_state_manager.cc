// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fbpreprocessor/session_state_manager.h"

#include <sys/types.h>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/time/time.h>
#include <bindings/device_management_backend.pb.h>
#include <bindings/policy_common_definitions.pb.h>
#include <brillo/errors/error.h>
#include <dbus/bus.h>
#include <login_manager/proto_bindings/policy_descriptor.pb.h>
#include <session_manager/dbus-proxies.h>

#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/storage.h"

namespace {
constexpr char kSessionStateStarted[] = "started";
constexpr char kSessionStateStopped[] = "stopped";
constexpr int kNumberActiveSessionsUnknown = -1;

// crash-reporter will write the firmware dumps to the input directory, allow
// members of the group to write to that directory.
constexpr mode_t kInputDirMode = 03770;
// debugd will read the processed firmware dumps from the output directory,
// allow members of the group to read from that directory. Only fbpreprocessor
// is allowed to write.
constexpr mode_t kOutputDirMode = 0750;

// Allowlist of domains whose users can add firmware dumps to feedback reports.
constexpr int kDomainAllowlistSize = 2;
constexpr std::array<std::string_view, kDomainAllowlistSize> kDomainAllowlist{
    "@google.com", "@managedchrome.com"};

// Allowlist of accounts that can add firmware dumps to feedback reports. This
// allowlist is used for "special" accounts, typically test accounts, that do
// not belong to an allowlisted domain.
constexpr int kUserAllowlistSize = 1;
constexpr std::array<std::string_view, kUserAllowlistSize> kUserAllowlist{
    "testuser@gmail.com"};

// Settings of the UserFeedbackWithLowLevelDebugDataAllowed policy that allow
// the addition of WiFi firmware dumps to feedback reports.
constexpr int kPolicyOptionsSize = 2;
constexpr std::array<std::string_view, kPolicyOptionsSize> kPolicyOptions{
    "all", "wifi"};

// Add a delay when the user logs in before the policy is ready to be retrieved.
constexpr base::TimeDelta kDelayForFirstUserInit = base::Seconds(2);

bool IsFirmwareDumpPolicyAllowed(
    const enterprise_management::CloudPolicySettings& user_policy) {
  // The UserFeedbackWithLowLevelDebugDataAllowed policy is stored
  // in the CloudPolicySubProto1 protobuf embedded inside the
  // CloudPolicySetting protobuf.
  if (!user_policy.has_subproto1()) {
    LOG(INFO) << "No CloudPolicySubProto1 present.";
    return false;
  }
  if (!user_policy.subproto1().has_userfeedbackwithlowleveldebugdataallowed()) {
    LOG(INFO) << "No UserFeedbackWithLowLevelDebugDataAllowed policy.";
    return false;
  }
  enterprise_management::StringListPolicyProto policy =
      user_policy.subproto1().userfeedbackwithlowleveldebugdataallowed();
  if (!policy.has_value()) {
    LOG(INFO) << "UserFeedbackWithLowLevelDebugDataAllowed policy is not set.";
    return false;
  }

  for (int i = 0; i < policy.value().entries_size(); i++) {
    if (base::Contains(kPolicyOptions, policy.value().entries(i))) {
      LOG(INFO) << "Firmware dumps allowed for subsystem "
                << policy.value().entries(i);
      return true;
    }
  }
  LOG(INFO) << "Firmware dumps not allowed.";
  return false;
}

bool IsUserInAllowedDomain(std::string_view username) {
  for (std::string_view domain : kDomainAllowlist) {
    if (username.ends_with(domain)) {
      return true;
    }
  }
  return false;
}
}  // namespace

namespace fbpreprocessor {

SessionStateManager::SessionStateManager(Manager* manager, dbus::Bus* bus)
    : session_manager_proxy_(
          std::make_unique<org::chromium::SessionManagerInterfaceProxy>(bus)),
      active_sessions_num_(kNumberActiveSessionsUnknown),
      fw_dumps_allowed_by_policy_(false),
      manager_(manager) {
  session_manager_proxy_->RegisterSessionStateChangedSignalHandler(
      base::BindRepeating(&SessionStateManager::OnSessionStateChanged,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&SessionStateManager::OnSignalConnected,
                     weak_factory_.GetWeakPtr()));
}

void SessionStateManager::OnSessionStateChanged(const std::string& state) {
  LOG(INFO) << "Session state changed to " << state;

  if (state == kSessionStateStarted) {
    // Always check the number of active sessions, even if the primary user is
    // still the same, since we want to disable the feature if a secondary
    // session has been started.
    if (!UpdateActiveSessions()) {
      LOG(ERROR) << "Failed to retrieve active sessions.";
    }
    if (!primary_user_hash_.empty()) {
      LOG(INFO) << "Primary user already exists. Not updating primary user.";
      return;
    }
    if (!UpdatePrimaryUser()) {
      LOG(ERROR) << "Failed to update primary user.";
      return;
    }
    if (!UpdatePolicy()) {
      LOG(ERROR) << "Failed to retrieve policy.";
    }
    for (auto& observer : observers_) {
      observer.OnUserLoggedIn(primary_user_hash_);
    }
  } else if (state == kSessionStateStopped) {
    ResetPrimaryUser();
    for (auto& observer : observers_) {
      observer.OnUserLoggedOut();
    }
  }
}

void SessionStateManager::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void SessionStateManager::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

bool SessionStateManager::PrimaryUserInAllowlist() const {
  return IsUserInAllowedDomain(primary_user_) ||
         base::Contains(kUserAllowlist, primary_user_);
}

bool SessionStateManager::UpdatePolicy() {
  // When a user logs in for the first time there is a delay before the policy
  // is available. Wait a little bit before retrieving the policy.
  return manager_->task_runner()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&SessionStateManager::OnPolicyUpdated,
                     weak_factory_.GetWeakPtr()),
      kDelayForFirstUserInit);
}

void SessionStateManager::OnPolicyUpdated() {
  fw_dumps_allowed_by_policy_ = false;

  login_manager::PolicyDescriptor descriptor;
  descriptor.set_account_type(login_manager::ACCOUNT_TYPE_USER);
  descriptor.set_domain(login_manager::POLICY_DOMAIN_CHROME);
  descriptor.set_account_id(primary_user_);

  brillo::ErrorPtr error;
  std::vector<uint8_t> out_blob;
  std::string descriptor_string = descriptor.SerializeAsString();
  if (!session_manager_proxy_->RetrievePolicyEx(
          std::vector<uint8_t>(descriptor_string.begin(),
                               descriptor_string.end()),
          &out_blob, &error) ||
      error.get()) {
    LOG(ERROR) << "Failed to retrieve policy "
               << (error ? error->GetMessage() : "unknown error") << ".";
    return;
  }
  enterprise_management::PolicyFetchResponse response;
  if (!response.ParseFromArray(out_blob.data(), out_blob.size())) {
    LOG(ERROR) << "Failed to parse policy response";
    return;
  }

  enterprise_management::PolicyData policy_data;
  if (!policy_data.ParseFromArray(response.policy_data().data(),
                                  response.policy_data().size())) {
    LOG(ERROR) << "Failed to parse policy data.";
    return;
  }

  enterprise_management::CloudPolicySettings user_policy;
  if (!user_policy.ParseFromString(policy_data.policy_value())) {
    LOG(ERROR) << "Failed to parse user policy.";
    return;
  }

  fw_dumps_allowed_by_policy_ = IsFirmwareDumpPolicyAllowed(user_policy);
  LOG(INFO) << "Adding firmware dumps to feedback reports "
            << (fw_dumps_allowed_by_policy_ ? "" : "NOT ")
            << "allowed by policy.";
}

bool SessionStateManager::FirmwareDumpsAllowedByPolicy() const {
  return (active_sessions_num_ == 1) && PrimaryUserInAllowlist() &&
         fw_dumps_allowed_by_policy_;
}

std::optional<std::pair<std::string, std::string>>
SessionStateManager::RetrievePrimaryUser() {
  brillo::ErrorPtr error;
  std::string username;
  std::string sanitized_username;
  if (!session_manager_proxy_->RetrievePrimarySession(
          &username, &sanitized_username, &error) ||
      error.get()) {
    LOG(ERROR) << "Failed to retrieve primary session: " << error->GetMessage();
    return std::nullopt;
  }
  return std::make_pair(username, sanitized_username);
}

bool SessionStateManager::UpdatePrimaryUser() {
  auto primary_user = RetrievePrimaryUser();

  if (!primary_user.has_value()) {
    LOG(ERROR) << "Error while retrieving primary user.";
    return false;
  }

  if (primary_user->first.empty() || primary_user->second.empty()) {
    LOG(INFO) << "Primary user does not exist.";
    return false;
  }

  primary_user_.assign(primary_user->first);
  primary_user_hash_.assign(primary_user->second);
  LOG(INFO) << "Primary user updated.";

  if (!CreateUserDirectories()) {
    LOG(ERROR) << "Failed to create input/output directories.";
  }

  return true;
}

bool SessionStateManager::UpdateActiveSessions() {
  active_sessions_num_ = kNumberActiveSessionsUnknown;
  brillo::ErrorPtr error;
  std::map<std::string, std::string> sessions;
  if (!session_manager_proxy_->RetrieveActiveSessions(&sessions, &error) ||
      error.get()) {
    LOG(ERROR) << "Failed to retrieve active sessions: " << error->GetMessage();
    return false;
  }
  active_sessions_num_ = sessions.size();
  LOG(INFO) << "Found " << active_sessions_num_ << " active sessions.";
  return true;
}

void SessionStateManager::ResetPrimaryUser() {
  primary_user_.clear();
  primary_user_hash_.clear();
  active_sessions_num_ = kNumberActiveSessionsUnknown;
  fw_dumps_allowed_by_policy_ = false;
}

bool SessionStateManager::RefreshPrimaryUser() {
  std::string old_primary_user_hash = primary_user_hash_;
  ResetPrimaryUser();

  bool update_result = UpdatePrimaryUser();
  update_result = update_result && UpdateActiveSessions();
  update_result = update_result && UpdatePolicy();

  if (old_primary_user_hash.empty() && !primary_user_hash_.empty()) {
    for (auto& observer : observers_) {
      observer.OnUserLoggedIn(primary_user_hash_);
    }
  } else if (!old_primary_user_hash.empty() && primary_user_hash_.empty()) {
    for (auto& observer : observers_) {
      observer.OnUserLoggedOut();
    }
  }

  return update_result;
}

bool SessionStateManager::CreateUserDirectories() const {
  bool success = true;
  if (primary_user_hash_.empty()) {
    LOG(ERROR) << "Can't create input/output directories without daemon store.";
    return false;
  }
  base::FilePath root_dir =
      base::FilePath(kDaemonStorageRoot).Append(primary_user_hash_);
  base::File::Error error;
  if (!base::CreateDirectoryAndGetError(root_dir.Append(kInputDirectory),
                                        &error)) {
    LOG(ERROR) << "Failed to create input directory: "
               << base::File::ErrorToString(error);
    success = false;
  }
  if (chmod(root_dir.Append(kInputDirectory).value().c_str(), kInputDirMode)) {
    LOG(ERROR) << "chmod of input directory failed.";
    success = false;
  }

  if (!base::CreateDirectoryAndGetError(root_dir.Append(kProcessedDirectory),
                                        &error)) {
    LOG(ERROR) << "Failed to create output directory: "
               << base::File::ErrorToString(error);
    success = false;
  }
  if (chmod(root_dir.Append(kProcessedDirectory).value().c_str(),
            kOutputDirMode)) {
    LOG(ERROR) << "chmod of output directory failed.";
    success = false;
  }

  return success;
}

void SessionStateManager::OnSignalConnected(const std::string& interface_name,
                                            const std::string& signal_name,
                                            bool success) const {
  if (!success)
    LOG(ERROR) << "Failed to connect to signal " << signal_name
               << " of interface " << interface_name;
  if (success) {
    LOG(INFO) << "Connected to signal " << signal_name << " of interface "
              << interface_name;
  }
}

}  // namespace fbpreprocessor
