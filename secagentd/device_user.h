// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SECAGENTD_DEVICE_USER_H_
#define SECAGENTD_DEVICE_USER_H_

#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/functional/callback_helpers.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "bindings/chrome_device_policy.pb.h"
#include "bindings/device_management_backend.pb.h"
#include "cryptohome/proto_bindings/UserDataAuth.pb.h"
#include "login_manager/proto_bindings/policy_descriptor.pb.h"
#include "session_manager/dbus-proxies.h"
#include "user_data_auth/dbus-proxies.h"

namespace secagentd {

// https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/secagentd/proto/security_xdr_events.proto
// For full device user descriptions.
namespace device_user {
static constexpr char kUnaffiliatedPrefix[] = "UnaffiliatedUser-";
static constexpr char kEmpty[] = "";
static constexpr char kUnknown[] = "Unknown";
static constexpr char kGuest[] = "GuestUser";
// Local accounts.
static constexpr char kManagedGuest[] = "ManagedGuest";
static constexpr char kKioskApp[] = "KioskApp";
static constexpr char kWebKioskApp[] = "KioskApp";
static constexpr char kKioskAndroidApp[] = "KioskAndroidApp";
static constexpr char kSAML[] = "SAML-PublicSession";
}  // namespace device_user

using DeviceAccountType =
    enterprise_management::DeviceLocalAccountInfoProto_AccountType;

static constexpr base::TimeDelta kDelayForFirstUserInit = base::Seconds(2);
static constexpr char kStarted[] = "started";
static constexpr char kStopping[] = "stopping";
static constexpr char kStopped[] = "stopped";
static constexpr char kInit[] = "init";
static constexpr char kSecagentdDirectory[] = "var/lib/secagentd";

namespace testing {
class DeviceUserTestFixture;
}  // namespace testing

class DeviceUserInterface : public base::RefCounted<DeviceUserInterface> {
 public:
  virtual void RegisterSessionChangeHandler() = 0;
  virtual void RegisterScreenLockedHandler(
      base::RepeatingClosure signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) = 0;
  virtual void RegisterScreenUnlockedHandler(
      base::RepeatingClosure signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) = 0;
  virtual void RegisterSessionChangeListener(
      base::RepeatingCallback<void(const std::string&)> cb) = 0;
  virtual void RegisterRemoveCompletedHandler() = 0;
  virtual void GetDeviceUserAsync(
      base::OnceCallback<void(const std::string& device_user)> cb) = 0;
  virtual std::list<std::string> GetUsernamesForRedaction() = 0;
  virtual bool GetIsUnaffiliated() = 0;
  virtual std::string GetUsernameBasedOnAffiliation(
      const std::string& username, const std::string& sanitized_username) = 0;
  virtual void SetFlushCallback(base::RepeatingCallback<void()>) = 0;

  virtual ~DeviceUserInterface() = default;
};

class DeviceUser : public DeviceUserInterface {
 public:
  explicit DeviceUser(
      std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
          session_manager_);

  // Allow calling the private test-only constructor without befriending
  // scoped_refptr.
  template <typename... Args>
  static scoped_refptr<DeviceUser> CreateForTesting(Args&&... args) {
    return base::WrapRefCounted(new DeviceUser(std::forward<Args>(args)...));
  }

  // Start monitoring for login/out events.
  // Called when XDR reporting becomes enabled.
  void RegisterSessionChangeHandler() override;
  // Registers for signal when the screen is locked.
  void RegisterScreenLockedHandler(
      base::RepeatingClosure signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) override;
  // Registers for signal when the screen is Unlocked.
  void RegisterScreenUnlockedHandler(
      base::RepeatingClosure signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) override;
  // Registers a callback to be notified when the session state changes.
  void RegisterSessionChangeListener(
      base::RepeatingCallback<void(const std::string&)> cb) override;
  // Starts listening for RemoveCompleted signal.
  void RegisterRemoveCompletedHandler() override;
  // Returns the current device user.
  void GetDeviceUserAsync(
      base::OnceCallback<void(const std::string& device_user)> cb) override;
  // Returns the most recently used usernames so they can be redacted.
  std::list<std::string> GetUsernamesForRedaction() override;
  // Returns if the user is unaffilaited to determine if events should be
  // reported.
  bool GetIsUnaffiliated() override;
  // Returns either the username when affiliated or the random UUID for
  // unaffiliated users.
  // If affiliation cannot be determined mark as Unknown.
  std::string GetUsernameBasedOnAffiliation(
      const std::string& username,
      const std::string& sanitized_username) override;
  // The flush callback to be called when a new sign in occurs to avoid
  // reporting unaffiliated events.
  void SetFlushCallback(base::RepeatingCallback<void()>) override;

  DeviceUser(const DeviceUser&) = delete;
  DeviceUser(DeviceUser&&) = delete;
  DeviceUser& operator=(const DeviceUser&) = delete;
  DeviceUser& operator=(DeviceUser&&) = delete;

 private:
  friend class testing::DeviceUserTestFixture;

  explicit DeviceUser(
      std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
          session_manager,
      std::unique_ptr<org::chromium::UserDataAuthInterfaceProxyInterface>
          cryptohome_proxy,
      const base::FilePath& root_path);

  // Logs an error if registering for session changes fails.
  void OnRegistrationResult(const std::string& interface,
                            const std::string& signal,
                            bool success);
  // Handles when a user is removed from the device.
  void OnRemoveCompleted(
      const user_data_auth::RemoveCompleted& remove_completed);
  // Handles if Session Manager's name changes, gipossibly indicating a crash.
  void OnSessionManagerNameChange(const std::string& old_owner,
                                  const std::string& new_owner);
  // Handles when there is a login/out event.
  void OnSessionStateChange(const std::string& state);
  // Updates the device id after a session change.
  void UpdateDeviceId();
  // Updates the user after a session change.
  bool UpdateDeviceUser(const std::string& state);
  // Retrieves the policy for the given account type and id.
  absl::StatusOr<enterprise_management::PolicyData> RetrievePolicy(
      login_manager::PolicyAccountType account_type,
      const std::string& account_id);
  // Return whether the current user is affiliated.
  bool IsAffiliated(const enterprise_management::PolicyData& user_policy);
  // Returns true if the given username is a local account (kiosk, managed
  // guest, etc.) and updates the device user.
  bool SetDeviceUserIfLocalAccount(std::string& username);
  // Handles setting the device user after affiliation is checked and writing
  // the username to disk.
  // Also notifies listeners that the user should have been updated.
  void HandleUserPolicyAndNotifyListeners(const std::string& state,
                                          std::string username,
                                          base::FilePath username_file);

  base::WeakPtrFactory<DeviceUser> weak_ptr_factory_;
  // Flushes all plugin batches.
  base::RepeatingCallback<void()> flush_cb_ = base::DoNothing();
  std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
      session_manager_;
  std::unique_ptr<org::chromium::UserDataAuthInterfaceProxyInterface>
      cryptohome_proxy_;
  std::vector<base::RepeatingCallback<void(const std::string&)>>
      session_change_listeners_;
  std::vector<base::OnceCallback<void(const std::string&)>>
      on_device_user_ready_cbs_;
  std::string device_user_ = device_user::kEmpty;
  std::list<std::string> redacted_usernames_;
  std::string device_id_ = "";
  const base::FilePath root_path_;
  const std::unordered_map<DeviceAccountType, std::string> local_account_map_ =
      {{enterprise_management::DeviceLocalAccountInfoProto::
            ACCOUNT_TYPE_PUBLIC_SESSION,
        device_user::kManagedGuest},
       {enterprise_management::DeviceLocalAccountInfoProto::
            ACCOUNT_TYPE_KIOSK_APP,
        device_user::kKioskApp},
       {enterprise_management::DeviceLocalAccountInfoProto::
            ACCOUNT_TYPE_KIOSK_ANDROID_APP,
        device_user::kKioskAndroidApp},
       {enterprise_management::DeviceLocalAccountInfoProto::
            ACCOUNT_TYPE_SAML_PUBLIC_SESSION,
        device_user::kSAML},
       {enterprise_management::DeviceLocalAccountInfoProto::
            ACCOUNT_TYPE_WEB_KIOSK_APP,
        device_user::kWebKioskApp}};
  bool device_user_ready_ = false;
};

}  // namespace secagentd
#endif  // SECAGENTD_DEVICE_USER_H_
