// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef AUTHPOLICY_AUTHPOLICY_H_
#define AUTHPOLICY_AUTHPOLICY_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/ref_counted.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <dbus/object_proxy.h>
#include <install_attributes/libinstallattributes.h>

#include "authpolicy/authpolicy_metrics.h"
#include "authpolicy/org.chromium.AuthPolicy.h"
#include "authpolicy/samba_interface.h"

using brillo::dbus_utils::AsyncEventSequencer;

namespace login_manager {
class PolicyDescriptor;
}

namespace authpolicy {

class ActiveDirectoryAccountInfo;
class Anonymizer;
class AuthPolicyMetrics;
class PathService;
class ResponseTracker;

extern const char kChromeUserPolicyType[];
extern const char kChromeDevicePolicyType[];
extern const char kChromeExtensionPolicyType[];

// Implementation of authpolicy's D-Bus interface. Mainly routes stuff between
// D-Bus and SambaInterface.
class AuthPolicy : public org::chromium::AuthPolicyAdaptor,
                   public org::chromium::AuthPolicyInterface {
 public:
  using PolicyResponseCallback =
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<int32_t>>;

  // Helper method to get the D-Bus object for the given |object_manager|.
  static std::unique_ptr<brillo::dbus_utils::DBusObject> GetDBusObject(
      brillo::dbus_utils::ExportedObjectManager* object_manager);

  AuthPolicy(AuthPolicyMetrics* metrics, const PathService* path_service);

  // Initializes internals. See SambaInterface::Initialize() for details.
  ErrorType Initialize(bool device_is_locked);

  // Registers the D-Bus object and interfaces.
  void RegisterAsync(
      std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object,
      const AsyncEventSequencer::CompletionAction& completion_callback);

  // Cleans all persistent state files. Returns true if all files were cleared.
  static bool CleanState(const PathService* path_service) {
    return SambaInterface::CleanState(path_service);
  }

  // org::chromium::AuthPolicyInterface: (see org.chromium.AuthPolicy.xml).

  // |auth_user_request_blob| is a serialized AuthenticateUserRequest protobuf.
  // |account_info_blob| is a serialized ActiveDirectoryAccountInfo protobuf.
  void AuthenticateUser(const std::vector<uint8_t>& auth_user_request_blob,
                        const dbus::FileDescriptor& password_fd,
                        int32_t* error,
                        std::vector<uint8_t>* account_info_blob) override;

  // |get_status_request_blob| is a serialized GetUserStatusRequest protobuf.
  // |user_status_blob| is a serialized ActiveDirectoryUserStatus protobuf.
  void GetUserStatus(const std::vector<uint8_t>& get_status_request_blob,
                     int32_t* error,
                     std::vector<uint8_t>* user_status_blob) override;

  // |kerberos_files_blob| is a serialized KerberosFiles profobuf.
  void GetUserKerberosFiles(const std::string& account_id,
                            int32_t* error,
                            std::vector<uint8_t>* kerberos_files_blob) override;

  // |join_domain_request_blob| is a serialized JoinDomainRequest protobuf.
  void JoinADDomain(const std::vector<uint8_t>& join_domain_request_blob,
                    const dbus::FileDescriptor& password_fd,
                    int32_t* error,
                    std::string* joined_domain) override;

  void RefreshUserPolicy(PolicyResponseCallback callback,
                         const std::string& acccount_id) override;

  void RefreshDevicePolicy(PolicyResponseCallback callback) override;

  std::string SetDefaultLogLevel(int32_t level) override;

  // Disable retry sleep for unit tests.
  void DisableRetrySleepForTesting() { samba_.DisableRetrySleepForTesting(); }

  // Returns the anonymizer.
  const Anonymizer* GetAnonymizerForTesting() const {
    return samba_.GetAnonymizerForTesting();
  }

  // Renew the user ticket-granting-ticket.
  ErrorType RenewUserTgtForTesting() { return samba_.RenewUserTgtForTesting(); }

  void SetDeviceIsLockedForTesting() { device_is_locked_ = true; }

 private:
  // Gets triggered by when the Kerberos credential cache or the configuration
  // file of the currently logged in user change. Triggers the
  // UserKerberosFilesChanged signal.
  void OnUserKerberosFilesChanged();

  // Sends policy to SessionManager. Assumes |gpo_policy_data| contains user
  // policy if |account_id_key| is not nullptr, otherwise assumes it's device
  // policy.
  void StorePolicy(std::unique_ptr<protos::GpoPolicyData> gpo_policy_data,
                   const std::string* account_id_key,
                   std::unique_ptr<ScopedTimerReporter> timer,
                   PolicyResponseCallback callback);

  // Sends a single policy blob to Session Manager. |policy_type| is the policy
  // type passed into enterprise_management::PolicyData. |response_tracker| is
  // a data structure to track all responses from Session Manager.
  void StoreSinglePolicy(const login_manager::PolicyDescriptor& descriptor,
                         const char* policy_type,
                         const std::string& policy_blob,
                         scoped_refptr<ResponseTracker> response_tracker);

  // Response callback from SessionManager, logs the result and calls callback.
  void OnPolicyStored(login_manager::PolicyDescriptor descriptor,
                      scoped_refptr<ResponseTracker> response_tracker,
                      dbus::Response* response);

  AuthPolicyMetrics* metrics_;  // Not owned.
  SambaInterface samba_;

  // Used during enrollment when authpolicyd cannot send policy to Session
  // Manager because device is not locked yet.
  std::unique_ptr<protos::GpoPolicyData> cached_device_policy_data_;
  bool device_is_locked_ = false;

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
  dbus::ObjectProxy* session_manager_proxy_ = nullptr;
  base::WeakPtrFactory<AuthPolicy> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(AuthPolicy);
};

}  // namespace authpolicy

#endif  // AUTHPOLICY_AUTHPOLICY_H_
