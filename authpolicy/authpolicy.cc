// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "authpolicy/authpolicy.h"

#include <utility>
#include <vector>

#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread_task_runner_handle.h>
#include <dbus/authpolicy/dbus-constants.h>
#include <login_manager/proto_bindings/policy_descriptor.pb.h>

#include "authpolicy/authpolicy_metrics.h"
#include "authpolicy/cryptohome_client.h"
#include "authpolicy/log_colors.h"
#include "authpolicy/path_service.h"
#include "authpolicy/proto_bindings/active_directory_info.pb.h"
#include "authpolicy/samba_helper.h"
#include "authpolicy/samba_interface.h"
#include "authpolicy/session_manager_client.h"
#include "bindings/device_management_backend.pb.h"

namespace em = enterprise_management;

using brillo::dbus_utils::DBusObject;
using login_manager::PolicyDescriptor;

namespace authpolicy {

constexpr char kChromeUserPolicyType[] = "google/chromeos/user";
constexpr char kChromeDevicePolicyType[] = "google/chromeos/device";
constexpr char kChromeExtensionPolicyType[] = "google/chrome/extension";

namespace {

// Returns true if the given |domain| is expected to be associated with a
// component id in PolicyDescriptor, e.g. an extension id for
// POLICY_DOMAIN_EXTENSIONS.
bool DomainRequiresComponentId(login_manager::PolicyDomain domain) {
  switch (domain) {
    case login_manager::POLICY_DOMAIN_CHROME:
      return false;
    case login_manager::POLICY_DOMAIN_EXTENSIONS:
    case login_manager::POLICY_DOMAIN_SIGNIN_EXTENSIONS:
      // The component id is the extension id.
      return true;
  }
  NOTREACHED() << "Invalid domain";
}

void PrintError(const char* msg, ErrorType error) {
  if (error == ERROR_NONE) {
    LOG(INFO) << kColorRequestSuccess << msg << " succeeded" << kColorReset;
  } else {
    LOG(INFO) << kColorRequestFail << msg << " failed with code " << error
              << kColorReset;
  }
}

ErrorMetricType GetPolicyErrorMetricType(bool is_refresh_user_policy) {
  return is_refresh_user_policy ? ERROR_OF_REFRESH_USER_POLICY
                                : ERROR_OF_REFRESH_DEVICE_POLICY;
}

// Serializes |proto| to the byte array |proto_blob|. Returns ERROR_NONE on
// success and ERROR_PARSE_FAILED otherwise.
WARN_UNUSED_RESULT ErrorType
SerializeProto(const google::protobuf::MessageLite& proto,
               std::vector<uint8_t>* proto_blob) {
  proto_blob->resize(proto.ByteSizeLong());
  if (!proto.SerializeToArray(proto_blob->data(), proto_blob->size())) {
    LOG(ERROR) << "Failed to serialize proto";
    return ERROR_PARSE_FAILED;
  }
  return ERROR_NONE;
}

WARN_UNUSED_RESULT ErrorType
ParseProto(google::protobuf::MessageLite* proto,
           const std::vector<uint8_t>& proto_blob) {
  if (!proto->ParseFromArray(proto_blob.data(), proto_blob.size())) {
    LOG(ERROR) << "Failed to parse proto";
    return ERROR_PARSE_FAILED;
  }
  return ERROR_NONE;
}

}  // namespace

// Tracks responses from D-Bus calls to Session Manager's StorePolicy during a
// Refresh*Policy call to AuthPolicy. StorePolicy is called N + 1 times (once
// for the main user/device policy and N times for extension policies, once per
// extension). The Refresh*Policy response callback is only called after all
// StorePolicy responses have been received. This class counts the responses and
// calls the Refresh*Policy response callback after the last response has been
// received. For tracking purposes, a failure to call StorePolicy (e.g. since
// parameters failed to serialize) counts as received response.
class ResponseTracker : public base::RefCountedThreadSafe<ResponseTracker> {
 public:
  ResponseTracker(bool is_refresh_user_policy,
                  int total_response_count,
                  AuthPolicyMetrics* metrics,
                  std::unique_ptr<ScopedTimerReporter> timer,
                  AuthPolicy::PolicyResponseCallback callback)
      : is_refresh_user_policy_(is_refresh_user_policy),
        outstanding_response_count_(total_response_count),
        metrics_(metrics),
        timer_(std::move(timer)),
        callback_(std::move(callback)) {}

  // Should be called when a response finished either successfully or not or if
  // the corresponding StorePolicy call was never made, e.g. due to an error on
  // call parameter setup. If |error_message| is empty, assumes that the
  // StorePolicy call succeeded.
  void OnResponseFinished(bool success) {
    if (!success)
      all_responses_succeeded_ = false;

    // Don't use DCHECK here since bad policy store call counting could have
    // security implications.
    CHECK_GT(outstanding_response_count_, 0);
    if (--outstanding_response_count_ == 0) {
      // This is the last response, call the callback.
      const ErrorMetricType metric_type =
          GetPolicyErrorMetricType(is_refresh_user_policy_);
      ErrorType error =
          all_responses_succeeded_ ? ERROR_NONE : ERROR_STORE_POLICY_FAILED;
      metrics_->ReportError(metric_type, error);
      callback_->Return(error);

      const char* request =
          is_refresh_user_policy_ ? "RefreshUserPolicy" : "RefreshDevicePolicy";
      PrintError(request, error);

      // Destroy the timer, which triggers the metric. It's going to be
      // destroyed with this instance, anyway, but doing it here explicitly is
      // easier to follow.
      timer_.reset();
    }
  }

 private:
  bool is_refresh_user_policy_;
  int outstanding_response_count_;
  AuthPolicyMetrics* metrics_;  // Not owned.
  std::unique_ptr<ScopedTimerReporter> timer_;
  AuthPolicy::PolicyResponseCallback callback_;
  bool all_responses_succeeded_ = true;
};

// static
std::unique_ptr<DBusObject> AuthPolicy::GetDBusObject(
    brillo::dbus_utils::ExportedObjectManager* object_manager) {
  return std::make_unique<DBusObject>(
      object_manager, object_manager->GetBus(),
      org::chromium::AuthPolicyAdaptor::GetObjectPath());
}

AuthPolicy::AuthPolicy(AuthPolicyMetrics* metrics,
                       const PathService* path_service)
    : org::chromium::AuthPolicyAdaptor(this),
      metrics_(metrics),
      samba_(metrics,
             path_service,
             base::Bind(&AuthPolicy::OnUserKerberosFilesChanged,
                        base::Unretained(this))) {}

AuthPolicy::~AuthPolicy() {}

ErrorType AuthPolicy::Initialize(bool device_is_locked) {
  device_is_locked_ = device_is_locked;
  return samba_.Initialize(device_is_locked_ /* expect_config */);
}

void AuthPolicy::RegisterAsync(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object,
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
        completion_callback) {
  DCHECK(!dbus_object_);
  dbus_object_ = std::move(dbus_object);

  // Make sure the task runner used in some places is actually the D-Bus task
  // runner. This guarantees that tasks scheduled on the task runner won't
  // interfere with D-Bus calls.
  CHECK_EQ(base::ThreadTaskRunnerHandle::Get(),
           dbus_object_->GetBus()->GetDBusTaskRunner());
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(completion_callback);

  session_manager_client_ =
      std::make_unique<SessionManagerClient>(dbus_object_.get());

  // Listen to session state changes for backing up user TGT and other data.
  session_manager_client_->ConnectToSessionStateChangedSignal(base::Bind(
      &SambaInterface::OnSessionStateChanged, base::Unretained(&samba_)));

  // Set proper session state.
  samba_.OnSessionStateChanged(session_manager_client_->RetrieveSessionState());

  // Give Samba access to Cryptohome.
  samba_.SetCryptohomeClient(
      std::make_unique<CryptohomeClient>(dbus_object_.get()));
}

void AuthPolicy::AuthenticateUser(
    const std::vector<uint8_t>& auth_user_request_blob,
    const base::ScopedFD& password_fd,
    int32_t* int_error,
    std::vector<uint8_t>* account_info_blob) {
  LOG(INFO) << kColorRequest << "Received 'AuthenticateUser' request"
            << kColorReset;
  ScopedTimerReporter timer(TIMER_AUTHENTICATE_USER);
  authpolicy::AuthenticateUserRequest request;
  ErrorType error = ParseProto(&request, auth_user_request_blob);

  ActiveDirectoryAccountInfo account_info;
  if (error == ERROR_NONE) {
    error = samba_.AuthenticateUser(request.user_principal_name(),
                                    request.account_id(), password_fd.get(),
                                    &account_info);
  }
  if (error == ERROR_NONE)
    error = SerializeProto(account_info, account_info_blob);

  PrintError("AuthenticateUser", error);
  metrics_->ReportError(ERROR_OF_AUTHENTICATE_USER, error);
  *int_error = static_cast<int>(error);
}

void AuthPolicy::GetUserStatus(
    const std::vector<uint8_t>& get_status_request_blob,
    int32_t* int_error,
    std::vector<uint8_t>* user_status_blob) {
  LOG(INFO) << kColorRequest << "Received 'GetUserStatus' request"
            << kColorReset;
  ScopedTimerReporter timer(TIMER_GET_USER_STATUS);
  authpolicy::GetUserStatusRequest request;
  ErrorType error = ParseProto(&request, get_status_request_blob);

  ActiveDirectoryUserStatus user_status;
  if (error == ERROR_NONE) {
    error = samba_.GetUserStatus(request.user_principal_name(),
                                 request.account_id(), &user_status);
  }
  if (error == ERROR_NONE)
    error = SerializeProto(user_status, user_status_blob);

  PrintError("GetUserStatus", error);
  metrics_->ReportError(ERROR_OF_GET_USER_STATUS, error);
  *int_error = static_cast<int>(error);
}

void AuthPolicy::GetUserKerberosFiles(
    const std::string& account_id,
    int32_t* int_error,
    std::vector<uint8_t>* kerberos_files_blob) {
  LOG(INFO) << kColorRequest << "Received 'GetUserKerberosFiles' request"
            << kColorReset;
  ScopedTimerReporter timer(TIMER_GET_USER_KERBEROS_FILES);

  KerberosFiles kerberos_files;
  ErrorType error = samba_.GetUserKerberosFiles(account_id, &kerberos_files);
  if (error == ERROR_NONE)
    error = SerializeProto(kerberos_files, kerberos_files_blob);
  PrintError("GetUserKerberosFiles", error);
  metrics_->ReportError(ERROR_OF_GET_USER_KERBEROS_FILES, error);
  *int_error = static_cast<int>(error);
}

void AuthPolicy::JoinADDomain(
    const std::vector<uint8_t>& join_domain_request_blob,
    const base::ScopedFD& password_fd,
    int32_t* int_error,
    std::string* joined_domain) {
  LOG(INFO) << kColorRequest << "Received 'JoinADDomain' request"
            << kColorReset;
  ScopedTimerReporter timer(TIMER_JOIN_AD_DOMAIN);

  JoinDomainRequest request;
  ErrorType error = ParseProto(&request, join_domain_request_blob);

  if (error == ERROR_NONE) {
    std::vector<std::string> machine_ou(request.machine_ou().begin(),
                                        request.machine_ou().end());

    error = samba_.JoinMachine(request.machine_name(), request.machine_domain(),
                               machine_ou, request.user_principal_name(),
                               request.kerberos_encryption_types(),
                               password_fd.get(), joined_domain);
  }

  PrintError("JoinADDomain", error);
  metrics_->ReportError(ERROR_OF_JOIN_AD_DOMAIN, error);
  *int_error = static_cast<int>(error);
}

void AuthPolicy::RefreshUserPolicy(PolicyResponseCallback callback,
                                   const std::string& account_id) {
  LOG(INFO) << kColorRequest << "Received 'RefreshUserPolicy' request"
            << kColorReset;
  auto timer = std::make_unique<ScopedTimerReporter>(TIMER_REFRESH_USER_POLICY);

  // Fetch GPOs for the current user.
  auto gpo_policy_data = std::make_unique<protos::GpoPolicyData>();
  ErrorType error = samba_.FetchUserGpos(account_id, gpo_policy_data.get());

  // Return immediately on error.
  if (error != ERROR_NONE) {
    PrintError("RefreshUserPolicy", error);
    metrics_->ReportError(ERROR_OF_REFRESH_USER_POLICY, error);
    callback->Return(error);
    return;
  }

  // Send policy to Session Manager.
  const std::string account_id_key = GetAccountIdKey(account_id);
  StorePolicy(std::move(gpo_policy_data), &account_id_key, std::move(timer),
              std::move(callback));
}

void AuthPolicy::RefreshDevicePolicy(PolicyResponseCallback callback) {
  LOG(INFO) << kColorRequest << "Received 'RefreshDevicePolicy' request"
            << kColorReset;
  auto timer =
      std::make_unique<ScopedTimerReporter>(TIMER_REFRESH_DEVICE_POLICY);

  if (cached_device_policy_data_) {
    // Send policy to Session Manager.
    LOG(INFO) << "Using cached policy";
    StorePolicy(std::move(cached_device_policy_data_), nullptr,
                std::move(timer), std::move(callback));
    return;
  }

  // Fetch GPOs for the device.
  auto gpo_policy_data = std::make_unique<protos::GpoPolicyData>();
  ErrorType error = samba_.FetchDeviceGpos(gpo_policy_data.get());

  device_is_locked_ = device_is_locked_ || InstallAttributesReader().IsLocked();
  if (!device_is_locked_ && error == ERROR_NONE) {
    LOG(INFO) << "Device is not locked yet. Caching device policy.";
    cached_device_policy_data_ = std::move(gpo_policy_data);
    error = ERROR_DEVICE_POLICY_CACHED_BUT_NOT_SENT;
  }

  // Return immediately on error.
  if (error != ERROR_NONE) {
    PrintError("RefreshDevicePolicy", error);
    metrics_->ReportError(ERROR_OF_REFRESH_DEVICE_POLICY, error);
    callback->Return(error);
    return;
  }

  // Send policy to Session Manager.
  StorePolicy(std::move(gpo_policy_data), nullptr, std::move(timer),
              std::move(callback));
}

std::string AuthPolicy::SetDefaultLogLevel(int32_t level) {
  LOG(INFO) << kColorRequest << "Received 'SetDefaultLogLevel' request"
            << kColorReset;
  if (level < AuthPolicyFlags::kMinLevel ||
      level > AuthPolicyFlags::kMaxLevel) {
    std::string message = base::StringPrintf("Level must be between %i and %i.",
                                             AuthPolicyFlags::kMinLevel,
                                             AuthPolicyFlags::kMaxLevel);
    LOG(ERROR) << message;
    return message;
  }
  samba_.SetDefaultLogLevel(static_cast<AuthPolicyFlags::DefaultLevel>(level));
  return std::string();
}

void AuthPolicy::OnUserKerberosFilesChanged() {
  LOG(INFO) << "Firing signal UserKerberosFilesChanged";
  SendUserKerberosFilesChangedSignal();
}

void AuthPolicy::StorePolicy(
    std::unique_ptr<protos::GpoPolicyData> gpo_policy_data,
    const std::string* account_id_key,
    std::unique_ptr<ScopedTimerReporter> timer,
    PolicyResponseCallback callback) {
  // Count total number of StorePolicy responses we're expecting and create a
  // tracker object that counts the number of outstanding responses and keeps
  // some unique pointers.
  const bool is_refresh_user_policy = account_id_key != nullptr;
  const int num_extensions = gpo_policy_data->extension_policies_size();
  const int num_store_policy_calls = 1 + num_extensions;

  LOG(INFO) << "Sending " << (is_refresh_user_policy ? "user" : "device")
            << " policy to Session Manager (Chrome policy, " << num_extensions
            << " extensions)";

  scoped_refptr<ResponseTracker> response_tracker =
      new ResponseTracker(is_refresh_user_policy, num_store_policy_calls,
                          metrics_, std::move(timer), std::move(callback));

  PolicyDescriptor descriptor;
  const char* policy_type = nullptr;
  if (is_refresh_user_policy) {
    DCHECK(!account_id_key->empty());
    descriptor.set_account_type(login_manager::ACCOUNT_TYPE_USER);
    descriptor.set_account_id(*account_id_key);
    policy_type = kChromeUserPolicyType;
  } else {
    descriptor.set_account_type(login_manager::ACCOUNT_TYPE_DEVICE);
    policy_type = kChromeDevicePolicyType;
  }

  // For double checking we counted the number of store calls right.
  int store_policy_call_count = 0;

  // Store the user or device policy.
  descriptor.set_domain(login_manager::POLICY_DOMAIN_CHROME);
  StoreSinglePolicy(descriptor, policy_type,
                    gpo_policy_data->user_or_device_policy(), response_tracker);
  store_policy_call_count++;

  // Store extension policies.
  descriptor.set_domain(login_manager::POLICY_DOMAIN_EXTENSIONS);
  for (int n = 0; n < num_extensions; ++n) {
    const protos::ExtensionPolicy& extension_policy =
        gpo_policy_data->extension_policies(n);
    descriptor.set_component_id(extension_policy.id());
    StoreSinglePolicy(descriptor, kChromeExtensionPolicyType,
                      extension_policy.json_data(), response_tracker);
    store_policy_call_count++;
  }

  // Don't use DCHECK here since bad policy store call counting could have
  // security implications.
  CHECK(store_policy_call_count == num_store_policy_calls);
}

void AuthPolicy::StoreSinglePolicy(
    const PolicyDescriptor& descriptor,
    const char* policy_type,
    const std::string& policy_blob,
    scoped_refptr<ResponseTracker> response_tracker) {
  // Wrap up the policy in a PolicyFetchResponse.
  em::PolicyData policy_data;
  policy_data.set_policy_value(policy_blob);
  policy_data.set_policy_type(policy_type);
  if (descriptor.account_type() == login_manager::ACCOUNT_TYPE_USER) {
    policy_data.set_username(samba_.GetUserPrincipal());
    // Device id in the proto also could be used as an account/client id.
    policy_data.set_device_id(samba_.user_account_id());
    if (samba_.is_user_affiliated())
      policy_data.add_user_affiliation_ids(kAffiliationMarker);
  } else {
    DCHECK(descriptor.account_type() == login_manager::ACCOUNT_TYPE_DEVICE);
    policy_data.set_device_id(samba_.machine_name());
    policy_data.add_device_affiliation_ids(kAffiliationMarker);
  }

  // TODO(crbug.com/831995): Use timer that can never run backwards and enable
  // timestamp validation in the Chromium Active Directory policy manager.
  policy_data.set_timestamp(base::Time::Now().ToJavaTime());
  policy_data.set_management_mode(em::PolicyData::ENTERPRISE_MANAGED);
  policy_data.set_machine_name(samba_.machine_name());
  if (DomainRequiresComponentId(descriptor.domain())) {
    DCHECK(!descriptor.component_id().empty());
    policy_data.set_settings_entity_id(descriptor.component_id());
  }

  // Note: No signature required here, Active Directory policy is unsigned!

  em::PolicyFetchResponse policy_response;
  std::string response_blob;
  if (!policy_data.SerializeToString(policy_response.mutable_policy_data()) ||
      !policy_response.SerializeToString(&response_blob)) {
    LOG(ERROR) << "Failed to serialize policy data";
    response_tracker->OnResponseFinished(false);
    return;
  }

  session_manager_client_->StoreUnsignedPolicyEx(
      descriptor.SerializeAsString(), response_blob,
      base::Bind(&ResponseTracker::OnResponseFinished, response_tracker));
}

}  // namespace authpolicy
