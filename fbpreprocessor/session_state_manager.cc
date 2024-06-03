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
#include <base/posix/eintr_wrapper.h>
#include <base/task/sequenced_task_runner.h>
#include <base/time/time.h>
#include <bindings/cloud_policy.pb.h>
#include <bindings/device_management_backend.pb.h>
#include <bindings/policy_common_definitions.pb.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/debugd/dbus-constants.h>
#include <chromeos/dbus/fbpreprocessor/dbus-constants.h>
#include <dbus/bus.h>
#include <debugd/dbus-proxies.h>
#include <login_manager/proto_bindings/policy_descriptor.pb.h>
#include <session_manager/dbus-proxies.h>

#include "fbpreprocessor/constants.h"
#include "fbpreprocessor/firmware_dump.h"
#include "fbpreprocessor/manager.h"
#include "fbpreprocessor/metrics.h"

namespace {
constexpr char kSessionStateStarted[] = "started";
constexpr char kSessionStateStopped[] = "stopped";
constexpr int kNumberActiveSessionsUnknown = -1;

// crash-reporter will write the firmware dumps to the input directory, allow
// members of the group to write to that directory.
constexpr mode_t kWritableByAccessGroupMembers = 03770;
// debugd will read the processed firmware dumps from the output directory,
// allow members of the group to read from that directory. Only fbpreprocessor
// is allowed to write.
constexpr mode_t kReadableByAccessGroupMembers = 0750;

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
// the addition of firmware dumps to feedback reports.
constexpr char kFwdumpPolicyAll[] = "all";
constexpr char kFwdumpPolicyWiFi[] = "wifi";
constexpr char kFwdumpPolicyBluetooth[] = "bluetooth";

// Add a delay when the user logs in before the policy is ready to be retrieved.
constexpr base::TimeDelta kDelayForFirstUserInit = base::Seconds(2);

bool IsFirmwareDumpPolicyAllowed(
    const enterprise_management::CloudPolicySettings& user_policy,
    fbpreprocessor::FirmwareDump::Type type) {
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
    // If the policy is set to "all" we consider connectivity fwdump policy as
    // enabled for all domains.
    if (policy.value().entries(i) == kFwdumpPolicyAll) {
      LOG(INFO) << "Firmware dumps allowed for all.";
      return true;
    }

    // If the policy is set to "wifi" we consider connectivity fwdump policy as
    // enabled for wifi domain.
    if ((type == fbpreprocessor::FirmwareDump::Type::kWiFi) &&
        (policy.value().entries(i) == kFwdumpPolicyWiFi)) {
      LOG(INFO) << "Firmware dumps allowed for wifi.";
      return true;
    }

    // If the policy is set to "bluetooth" we consider connectivity fwdump
    // policy as enabled for bluetooth domain.
    if ((type == fbpreprocessor::FirmwareDump::Type::kBluetooth) &&
        (policy.value().entries(i) == kFwdumpPolicyBluetooth)) {
      LOG(INFO) << "Firmware dumps allowed for bluetooth.";
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
      debugd_proxy_(std::make_unique<org::chromium::debugdProxy>(bus)),
      base_dir_(kDaemonStorageRoot),
      active_sessions_num_(kNumberActiveSessionsUnknown),
      wifi_fw_dumps_allowed_by_policy_(false),
      bluetooth_fw_dumps_allowed_by_policy_(false),
      fw_dumps_policy_loaded_(false),
      finch_loaded_(false),
      manager_(manager) {
  session_manager_proxy_->RegisterSessionStateChangedSignalHandler(
      base::BindRepeating(&SessionStateManager::OnSessionStateChanged,
                          weak_factory_.GetWeakPtr()),
      base::BindOnce(&SessionStateManager::OnSignalConnected,
                     weak_factory_.GetWeakPtr()));
  if (manager_->platform_features()) {
    manager_->platform_features()->AddObserver(this);
  }
}

SessionStateManager::~SessionStateManager() {
  if (manager_->platform_features()) {
    manager_->platform_features()->RemoveObserver(this);
  }
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
    HandleUserLogin();
  } else if (state == kSessionStateStopped) {
    ResetPrimaryUser();
    HandleUserLogout();
  }
}

void SessionStateManager::HandleUserLogin() {
  // |NotifyObserversOnUserLogin| is scheduled after the debug buffer clearing
  // task, whether the task is successful or not, to make sure the observers
  // will perform the rest of login tasks and handle them properly. The success
  // and failure cases of debug buffer clearing differ in the treatment of the
  // policy flag, which is handled in |OnClearFirmwareDumpBufferResponse| and
  // |OnClearFirmwareDumpBufferError|, respectively.
  // For example, the observer |input_manager| must delete all existing raw
  // dumps as one of the follow-up tasks upon user login, and therefore it is
  // required to notify observers for both cases. As for the sequence, buffer
  // clearing is required before old dump deletion to make sure old buffer will
  // not be included in new dumps. Likewise for other observers.
  debugd_proxy_->ClearFirmwareDumpBufferAsync(
      static_cast<uint32_t>(debugd::FirmwareDumpType::WIFI),
      /*success_callback=*/
      base::BindOnce(&SessionStateManager::OnClearFirmwareDumpBufferResponse,
                     weak_factory_.GetWeakPtr(), /*is_login=*/true)
          .Then(base::BindOnce(&SessionStateManager::NotifyObserversOnUserLogin,
                               weak_factory_.GetWeakPtr())),
      /*error_callback=*/
      base::BindOnce(&SessionStateManager::OnClearFirmwareDumpBufferError,
                     weak_factory_.GetWeakPtr())
          .Then(base::BindOnce(&SessionStateManager::NotifyObserversOnUserLogin,
                               weak_factory_.GetWeakPtr())));
}

void SessionStateManager::HandleUserLogout() {
  NotifyObserversOnUserLogout();
  debugd_proxy_->ClearFirmwareDumpBufferAsync(
      static_cast<uint32_t>(debugd::FirmwareDumpType::WIFI),
      /*success_callback=*/
      base::BindOnce(&SessionStateManager::OnClearFirmwareDumpBufferResponse,
                     weak_factory_.GetWeakPtr(), /*is_login=*/false),
      /*error_callback=*/
      base::BindOnce(&SessionStateManager::OnClearFirmwareDumpBufferError,
                     weak_factory_.GetWeakPtr()));
}

void SessionStateManager::OnClearFirmwareDumpBufferResponse(bool is_login,
                                                            bool success) {
  VLOG(kLocalDebugVerbosity) << __func__;
  if (!success) {
    LOG(ERROR) << "Request for clearing firmware dump buffer was responded, "
                  "but the firmware/driver execution failed.";
    // When buffer clearing fails, disable the feature from policy to avoid
    // potential policy violation from cross-session debug buffer.
    wifi_fw_dumps_allowed_by_policy_ = false;
    bluetooth_fw_dumps_allowed_by_policy_ = false;
    return;
  }
  LOG(INFO) << "Request for clearing firmware dump buffer was successful.";
  // For user login, the task that retrieves the policy must be performed after
  // the call to |ClearFirmwareDumpBufferAsync| being successful, to make sure
  // no new firmware dumps will be generated when there's potential
  // cross-session debug data in the buffer.
  if (is_login && !UpdatePolicy()) {
    LOG(ERROR) << "Failed to retrieve policy.";
  }
}

void SessionStateManager::OnClearFirmwareDumpBufferError(brillo::Error* error) {
  VLOG(kLocalDebugVerbosity) << __func__;
  LOG(ERROR) << "Failed to clear firmware dump buffer (" << error->GetCode()
             << "): " << error->GetMessage();
  // When buffer clearing fails, disable the feature from policy to avoid
  // potential policy violation from cross-session debug buffer.
  wifi_fw_dumps_allowed_by_policy_ = false;
  bluetooth_fw_dumps_allowed_by_policy_ = false;
}

void SessionStateManager::AddObserver(
    SessionStateManagerInterface::Observer* observer) {
  observers_.AddObserver(observer);
}

void SessionStateManager::RemoveObserver(
    SessionStateManagerInterface::Observer* observer) {
  observers_.RemoveObserver(observer);
}

void SessionStateManager::NotifyObserversOnUserLogin() {
  for (auto& observer : observers_) {
    observer.OnUserLoggedIn(primary_user_hash_);
  }
}

void SessionStateManager::NotifyObserversOnUserLogout() {
  for (auto& observer : observers_) {
    observer.OnUserLoggedOut();
  }
}

void SessionStateManager::OnFeatureChanged(bool allowed) {
  finch_loaded_ = true;
  EmitFeatureAllowedMetric();
}

void SessionStateManager::EmitFeatureAllowedMetric() {
  if (!finch_loaded_ || !fw_dumps_policy_loaded_) {
    // The state is not complete yet, either the policy or Finch have not yet
    // been queried.
    return;
  }

  // Check the allowed status for WiFi firmware dumps.
  Metrics::CollectionAllowedStatus status =
      Metrics::CollectionAllowedStatus::kAllowed;

  // The order of precedence of the reasons why the feature is disallowed must
  // remain constant over time. Do not modify.
  if (!manager_->platform_features()->FirmwareDumpsAllowedByFinch())
    status = Metrics::CollectionAllowedStatus::kDisallowedByFinch;
  else if (!PrimaryUserInAllowlist())
    status = Metrics::CollectionAllowedStatus::kDisallowedForUserDomain;
  else if (!wifi_fw_dumps_allowed_by_policy_)
    status = Metrics::CollectionAllowedStatus::kDisallowedByPolicy;
  else if (active_sessions_num_ != 1)
    status = Metrics::CollectionAllowedStatus::kDisallowedForMultipleSessions;

  manager_->metrics().SendAllowedStatus(FirmwareDump::Type::kWiFi, status);

  // Check the allowed status for Bluetooth firmware dumps.
  status = Metrics::CollectionAllowedStatus::kAllowed;

  // The order of precedence of the reasons why the feature is disallowed must
  // remain constant over time. Do not modify.
  if (!manager_->platform_features()->FirmwareDumpsAllowedByFinch())
    status = Metrics::CollectionAllowedStatus::kDisallowedByFinch;
  else if (!PrimaryUserInAllowlist())
    status = Metrics::CollectionAllowedStatus::kDisallowedForUserDomain;
  else if (!bluetooth_fw_dumps_allowed_by_policy_)
    status = Metrics::CollectionAllowedStatus::kDisallowedByPolicy;
  else if (active_sessions_num_ != 1)
    status = Metrics::CollectionAllowedStatus::kDisallowedForMultipleSessions;

  manager_->metrics().SendAllowedStatus(FirmwareDump::Type::kBluetooth, status);
}

bool SessionStateManager::PrimaryUserInAllowlist() const {
  return IsUserInAllowedDomain(primary_user_) ||
         base::Contains(kUserAllowlist, primary_user_);
}

// Fetch the policy from login_manager and see if
// |UserFeedbackWithLowLevelDebugDataAllowed| is set to allow firmware dumps.
// Returns true if fetching and parsing the policy was successful.
bool SessionStateManager::RetrieveAndParsePolicy(
    org::chromium::SessionManagerInterfaceProxyInterface* proxy,
    const login_manager::PolicyDescriptor& descriptor) {
  wifi_fw_dumps_allowed_by_policy_ = false;
  bluetooth_fw_dumps_allowed_by_policy_ = false;

  brillo::ErrorPtr error;
  std::vector<uint8_t> out_blob;
  std::string descriptor_string = descriptor.SerializeAsString();
  if (!proxy->RetrievePolicyEx(std::vector<uint8_t>(descriptor_string.begin(),
                                                    descriptor_string.end()),
                               &out_blob, &error) ||
      error.get()) {
    LOG(ERROR) << "Failed to retrieve policy "
               << (error ? error->GetMessage() : "unknown error") << ".";
    return false;
  }
  enterprise_management::PolicyFetchResponse response;
  if (!response.ParseFromArray(out_blob.data(), out_blob.size())) {
    LOG(ERROR) << "Failed to parse policy response";
    return false;
  }

  enterprise_management::PolicyData policy_data;
  if (!policy_data.ParseFromArray(response.policy_data().data(),
                                  response.policy_data().size())) {
    LOG(ERROR) << "Failed to parse policy data.";
    return false;
  }

  enterprise_management::CloudPolicySettings user_policy;
  if (!user_policy.ParseFromString(policy_data.policy_value())) {
    LOG(ERROR) << "Failed to parse user policy.";
    return false;
  }

  wifi_fw_dumps_allowed_by_policy_ =
      IsFirmwareDumpPolicyAllowed(user_policy, FirmwareDump::Type::kWiFi);
  bluetooth_fw_dumps_allowed_by_policy_ =
      IsFirmwareDumpPolicyAllowed(user_policy, FirmwareDump::Type::kBluetooth);

  return true;
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
  login_manager::PolicyDescriptor descriptor;
  descriptor.set_account_type(login_manager::ACCOUNT_TYPE_USER);
  descriptor.set_domain(login_manager::POLICY_DOMAIN_CHROME);
  descriptor.set_account_id(primary_user_);

  if (!RetrieveAndParsePolicy(session_manager_proxy_.get(), descriptor)) {
    LOG(ERROR) << "Failed to get policy.";
    return;
  }

  fw_dumps_policy_loaded_ = true;
  EmitFeatureAllowedMetric();
  LOG(INFO) << "Adding WiFi firmware dumps to feedback reports "
            << (wifi_fw_dumps_allowed_by_policy_ ? "" : "NOT ")
            << "allowed by policy.";
  LOG(INFO) << "Adding Bluetooth firmware dumps to feedback reports "
            << (bluetooth_fw_dumps_allowed_by_policy_ ? "" : "NOT ")
            << "allowed by policy.";
}

bool SessionStateManager::FirmwareDumpsAllowedByPolicy(
    FirmwareDump::Type type) const {
  if ((active_sessions_num_ != 1) || !PrimaryUserInAllowlist()) {
    return false;
  }

  switch (type) {
    case FirmwareDump::Type::kWiFi:
      return wifi_fw_dumps_allowed_by_policy_;
    case FirmwareDump::Type::kBluetooth:
      return bluetooth_fw_dumps_allowed_by_policy_;
  }

  return false;
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
  finch_loaded_ = false;
  fw_dumps_policy_loaded_ = false;
  wifi_fw_dumps_allowed_by_policy_ = false;
  bluetooth_fw_dumps_allowed_by_policy_ = false;
}

bool SessionStateManager::RefreshPrimaryUser() {
  std::string old_primary_user_hash = primary_user_hash_;
  ResetPrimaryUser();

  bool update_result = UpdatePrimaryUser();
  update_result = update_result && UpdateActiveSessions();

  if (old_primary_user_hash.empty() && !primary_user_hash_.empty()) {
    HandleUserLogin();
  } else if (!old_primary_user_hash.empty() && primary_user_hash_.empty()) {
    HandleUserLogout();
  }

  return update_result;
}

bool SessionStateManager::CreateUserDirectories() const {
  bool success = true;
  if (primary_user_hash_.empty()) {
    LOG(ERROR) << "Can't create input/output directories without daemon store.";
    return false;
  }
  base::FilePath root_dir = base_dir_.Append(primary_user_hash_);
  base::File::Error error;
  if (!base::CreateDirectoryAndGetError(root_dir.Append(kInputDirectory),
                                        &error)) {
    LOG(ERROR) << "Failed to create input directory: "
               << base::File::ErrorToString(error);
    success = false;
  }
  if (HANDLE_EINTR(chmod(root_dir.Append(kInputDirectory).value().c_str(),
                         kWritableByAccessGroupMembers))) {
    LOG(ERROR) << "chmod of input directory failed.";
    success = false;
  }

  if (!base::CreateDirectoryAndGetError(root_dir.Append(kProcessedDirectory),
                                        &error)) {
    LOG(ERROR) << "Failed to create output directory: "
               << base::File::ErrorToString(error);
    success = false;
  }
  if (HANDLE_EINTR(chmod(root_dir.Append(kProcessedDirectory).value().c_str(),
                         kReadableByAccessGroupMembers))) {
    LOG(ERROR) << "chmod of output directory failed.";
    success = false;
  }

  if (!base::CreateDirectoryAndGetError(root_dir.Append(kScratchDirectory),
                                        &error)) {
    LOG(ERROR) << "Failed to create scratch directory: "
               << base::File::ErrorToString(error);
    success = false;
  }
  if (HANDLE_EINTR(chmod(root_dir.Append(kScratchDirectory).value().c_str(),
                         kWritableByAccessGroupMembers))) {
    LOG(ERROR) << "chmod of scratch directory failed.";
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
