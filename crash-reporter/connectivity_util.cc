// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// connectivity_util.cc contains helper functions to implement the special
// rules around connectivity firmware dumps. These firmware dumps are not
// uploaded like normal crashes; instead, they are only collected for
// Googlers who have opted in, and they are only uploaded as part of a
// feedback report.

#include "crash-reporter/connectivity_util.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <base/containers/contains.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <bindings/cloud_policy.pb.h>
#include <bindings/device_management_backend.pb.h>
#include <brillo/userdb_utils.h>
#include <fbpreprocessor-client/fbpreprocessor/dbus-constants.h>
#include <login_manager/proto_bindings/policy_descriptor.pb.h>
#include <policy/device_policy.h>

#include "crash-reporter/paths.h"

namespace {

// Array to store all the allowed users to fetch connectivity
// fwdumps. This list is expected to grow as the tast tests are
// onboarded.
constexpr std::array<std::string_view, 1> kUserAllowlist{
    "testuser@managedchrome.com"};

// Allowlist of domains whose users can add firmware dumps to feedback reports.
constexpr int kDomainAllowlistSize = 2;
constexpr std::array<std::string_view, kDomainAllowlistSize> kDomainAllowlist{
    "@google.com", "@managedchrome.com"};

// Checks if the user is a googler or a google test account and
// returns true if that is the case.
bool IsUserInAllowedDomain(std::string_view username) {
  for (std::string_view domain : kDomainAllowlist) {
    if (username.ends_with(domain)) {
      return true;
    }
  }
  return false;
}

// This function checks if a given user is in connectivity fwdump collection
// allowlist, returns true if user is in allowlist else false.
bool IsUserInConnectivityFwdumpAllowlist(const std::string& username) {
  return base::Contains(kUserAllowlist, username);
}

// This function internally makes a call to RetrievePolicyEx to
// fetch user policy information and returns CloudPolicySettings.
std::optional<enterprise_management::CloudPolicySettings> FetchUserPolicy(
    org::chromium::SessionManagerInterfaceProxyInterface* session_manager_proxy,
    const std::string& username) {
  login_manager::PolicyDescriptor descriptor;
  descriptor.set_account_type(login_manager::ACCOUNT_TYPE_USER);
  descriptor.set_domain(login_manager::POLICY_DOMAIN_CHROME);
  descriptor.set_account_id(username);

  brillo::ErrorPtr error;
  std::vector<uint8_t> out_blob;
  std::string descriptor_string = descriptor.SerializeAsString();
  if (!session_manager_proxy->RetrievePolicyEx(
          std::vector<uint8_t>(descriptor_string.begin(),
                               descriptor_string.end()),
          &out_blob, &error) ||
      error.get()) {
    LOG(ERROR) << "Failed to retrieve policy "
               << (error ? error->GetMessage() : "unknown error") << ".";
    return std::nullopt;
  }

  enterprise_management::PolicyFetchResponse response;
  if (!response.ParseFromArray(out_blob.data(), out_blob.size())) {
    LOG(ERROR) << "Failed to parse policy response";
    return std::nullopt;
  }

  enterprise_management::PolicyData policy_data;
  if (!policy_data.ParseFromArray(response.policy_data().data(),
                                  response.policy_data().size())) {
    LOG(ERROR) << "Failed to parse policy data.";
    return std::nullopt;
  }

  enterprise_management::CloudPolicySettings user_policy_val;
  if (!user_policy_val.ParseFromString(policy_data.policy_value())) {
    LOG(ERROR) << "Failed to parse user policy.";
    return std::nullopt;
  }
  return user_policy_val;
}

// Checks if crash reporter is allowed to collect fw dump for given user.
bool ConnectivityFwdumpCollectionForUserAllowed(const std::string& username) {
  if (IsUserInAllowedDomain(username)) {
    return true;
  }

  return IsUserInConnectivityFwdumpAllowlist(username);
}

// Checks if connectivity fw dump collection policy is set.
bool IsFwdumpPolicySet(
    const enterprise_management::CloudPolicySettings& user_policy) {
  // UserFeedbackWithLowLevelDebugDataAllowed policy is stored
  // in CloudPolicySubProto1 protobuf embedded inside
  // CloudPolicySetting protobuf.
  if (!user_policy.has_subproto1()) {
    return false;
  }

  enterprise_management::CloudPolicySubProto1 subproto =
      user_policy.subproto1();
  if (!subproto.has_userfeedbackwithlowleveldebugdataallowed()) {
    LOG(INFO) << "UserFeedbackWithLowLevelDebugDataAllowed not set.";
    return false;
  }

  ::enterprise_management::StringListPolicyProto
      connectivity_fwdump_policy_val =
          subproto.userfeedbackwithlowleveldebugdataallowed();
  if (!connectivity_fwdump_policy_val.has_value()) {
    LOG(ERROR) << "UserFeedbackWithLowLevelDebugDataAllowed set"
               << "but has no policy value.";
    return false;
  }

  ::enterprise_management::StringList policy_string_list =
      connectivity_fwdump_policy_val.value();

  // UserFeedbackWithLowLevelDebugDataAllowed policy can have values
  // specific to a domain e.g. "wifi", "bluetooth" or "all". In case
  // it is "all", connectivity fwdumps for all the connectivity domains
  // can be enabled.
  for (int i = 0; i < policy_string_list.entries_size(); i++) {
    // If the policy is set to "wifi" or "all" we consider connectivity
    // fwdump policy as enabled for wifi domain.
    if ((policy_string_list.entries(i) == "wifi") ||
        (policy_string_list.entries(i) == "all")) {
      LOG(INFO) << "UserFeedbackWithLowLevelDebugDataAllowed is set.";
      return true;
    }
  }
  LOG(INFO) << "UserFeedbackWithLowLevelDebugDataAllowed policy is not set.";
  return false;
}

}  // namespace

namespace connectivity_util {

std::optional<Session> GetPrimaryUserSession(
    org::chromium::SessionManagerInterfaceProxyInterface*
        session_manager_proxy) {
  if (!session_manager_proxy) {
    LOG(ERROR) << "No session_manager_proxy for dbus call.";
    return std::nullopt;
  }

  brillo::ErrorPtr error;
  std::string username;
  std::string user_hash;
  if (!session_manager_proxy->RetrievePrimarySession(&username, &user_hash,
                                                     &error) ||
      error.get()) {
    LOG(ERROR) << "Failed to retrieve active sessions "
               << (error ? error->GetMessage() : "unknown error") << ".";
    return std::nullopt;
  }

  if (username.empty() || user_hash.empty()) {
    LOG(INFO) << "No primary user found.";
    return std::nullopt;
  }

  Session session_instance;
  session_instance.username = username;
  session_instance.userhash = user_hash;
  return session_instance;
}

bool IsConnectivityFwdumpAllowed(
    org::chromium::SessionManagerInterfaceProxyInterface* session_manager_proxy,
    const std::string& username) {
  if (!session_manager_proxy) {
    LOG(ERROR) << "No session_manager_proxy for dbus call.";
    return false;
  }

  if (!ConnectivityFwdumpCollectionForUserAllowed(username)) {
    LOG(INFO) << "Connectivity fwdump for the user not allowed, exiting.";
    return false;
  }

  std::optional<enterprise_management::CloudPolicySettings> user_policy =
      FetchUserPolicy(session_manager_proxy, username);
  if (!user_policy) {
    LOG(ERROR) << "Failed to fetch user policy.";
    return false;
  }

  return IsFwdumpPolicySet(*user_policy);
}

std::optional<base::FilePath> GetDaemonStoreFbPreprocessordDirectory(
    const Session& primary_session) {
  if (primary_session.userhash.empty()) {
    LOG(ERROR) << "No userhash found, exiting.";
    return std::nullopt;
  }
  return paths::GetAt(paths::kCryptohomeFbPreprocessorBaseDirectory,
                      primary_session.userhash)
      .Append(fbpreprocessor::kInputDirectory);
}
}  // namespace connectivity_util
