// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LOGIN_MANAGER_SESSION_MANAGER_IMPL_H_
#define LOGIN_MANAGER_SESSION_MANAGER_IMPL_H_

#include "login_manager/session_manager_interface.h"

#include <stdint.h>
#include <stdlib.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <brillo/dbus/dbus_method_response.h>
#include <brillo/errors/error.h>
#include <dbus/file_descriptor.h>

#include "login_manager/container_manager_interface.h"
#include "login_manager/device_policy_service.h"
#include "login_manager/key_generator.h"
#include "login_manager/policy_service.h"
#include "login_manager/regen_mitigator.h"
#include "login_manager/server_backed_state_key_generator.h"

class Crossystem;
class InstallAttributesReader;

namespace dbus {
class ObjectProxy;
class Response;
}  // namespace dbus

namespace login_manager {
class DBusSignalEmitterInterface;
class DeviceLocalAccountPolicyService;
class InitDaemonController;
class KeyGenerator;
class LoginMetrics;
class NssUtil;
class PolicyKey;
class ProcessManagerServiceInterface;
class StartArcInstanceRequest;
class SystemUtils;
class UserPolicyServiceFactory;
class VpdProcess;

// Implements the DBus SessionManagerInterface.
//
// All signatures used in the methods of the ownership API are
// SHA1 with RSA encryption.
class SessionManagerImpl : public SessionManagerInterface,
                           public KeyGenerator::Delegate,
                           public PolicyService::Delegate {
 public:
  // Magic user name strings.
  static const char kDemoUser[];

  // Payloads for SessionStateChanged DBus signal.
  static const char kStarted[];
  static const char kStopping[];
  static const char kStopped[];

  // Path to flag file indicating that a user has logged in since last boot.
  static const char kLoggedInFlag[];

  // Path to magic file that will trigger device wiping on next boot.
  static const char kResetFile[];

  // Name of init signal emitted when user session starts.
  static const char kStartUserSessionSignal[];

  // Name of the Android container.
  static const char kArcContainerName[];

  // Android container messages.
  static const char kArcStartForLoginScreenSignal[];
  static const char kArcStartSignal[];
  static const char kArcStopSignal[];
  static const char kArcNetworkStartSignal[];
  static const char kArcNetworkStopSignal[];
  static const char kArcBootedSignal[];
  static const char kArcRemoveOldDataSignal[];

  class Error {
   public:
    Error();
    Error(const std::string& name, const std::string& message);
    virtual ~Error();

    void Set(const std::string& name, const std::string& message);
    bool is_set() const { return set_; }
    const std::string& name() const { return name_; }
    const std::string& message() const { return message_; }

   private:
    std::string name_;
    std::string message_;
    bool set_;
  };

  SessionManagerImpl(std::unique_ptr<InitDaemonController> init_controller,
                     DBusSignalEmitterInterface* dbus_emitter,
                     base::Closure lock_screen_closure,
                     base::Closure restart_device_closure,
                     base::Closure start_arc_instance_closure,
                     base::Closure stop_arc_instance_closure,
                     KeyGenerator* key_gen,
                     ServerBackedStateKeyGenerator* state_key_generator,
                     ProcessManagerServiceInterface* manager,
                     LoginMetrics* metrics,
                     NssUtil* nss,
                     SystemUtils* utils,
                     Crossystem* crossystem,
                     VpdProcess* vpd_process,
                     PolicyKey* owner_key,
                     ContainerManagerInterface* android_container,
                     InstallAttributesReader* install_attributes_reader,
                     dbus::ObjectProxy* system_clock_proxy);
  virtual ~SessionManagerImpl();

  // Checks if string looks like a valid account ID key (as returned by
  // AccountId::GetAccountIdKey()).
  static bool ValidateAccountIdKey(const std::string& account_id);

  // Performs very, very basic validation of |email_address|.
  static bool ValidateEmail(const std::string& email_address);

#if USE_CHEETS
  // Validates if the given |request| satisfies the requirement of the
  // StartArcInstance input. Returns true on success. Otherwise false,
  // and brillo::Error instance is set to |error|.
  static bool ValidateStartArcInstanceRequest(
      const StartArcInstanceRequest& request,
      brillo::ErrorPtr* error);

  // Returns the Android data directory for |normalized_account_id|.
  static base::FilePath GetAndroidDataDirForUser(
      const std::string& normalized_account_id);

  // Returns the directory where old Android data directories are stored for
  // |normalized_account_id|.
  static base::FilePath GetAndroidDataOldDirForUser(
      const std::string& normalized_account_id);
#endif  // USE_CHEETS

  // Tests can call this before Initialize() to inject their own objects.
  void SetPolicyServicesForTest(
      std::unique_ptr<DevicePolicyService> device_policy,
      std::unique_ptr<UserPolicyServiceFactory> user_policy_factory,
      std::unique_ptr<DeviceLocalAccountPolicyService>
          device_local_account_policy);

  // SessionManagerInterface implementation.
  // Should set up policy stuff; if false DIE.
  bool Initialize() override;
  void Finalize() override;

  void AnnounceSessionStoppingIfNeeded() override;
  void AnnounceSessionStopped() override;
  bool ShouldEndSession() override;
  std::vector<std::string> GetStartUpFlags() override {
    return device_policy_->GetStartUpFlags();
  }

  // Starts a 'Powerwash' of the device by touching a flag file, then
  // rebooting to allow early-boot code to wipe parts of stateful we
  // need wiped. Have a look at /src/platform/init/chromeos_startup
  // for the gory details.
  void InitiateDeviceWipe(const std::string& reason) override;

  //////////////////////////////////////////////////////////////////////////////
  // Methods exposed via RPC are defined below.

  void EmitLoginPromptVisible();
  bool EnableChromeTesting(brillo::ErrorPtr* error,
                           bool in_force_relaunch,
                           const std::vector<std::string>& in_extra_arguments,
                           std::string* out_filepath);
  bool StartSession(brillo::ErrorPtr* error,
                    const std::string& in_account_id,
                    const std::string& in_unique_identifier);
  void StopSession(const std::string& in_unique_identifier);

  void StorePolicy(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
      const std::vector<uint8_t>& in_policy_blob);
  void StoreUnsignedPolicy(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
      const std::vector<uint8_t>& in_policy_blob);
  bool RetrievePolicy(
      brillo::ErrorPtr* error,
      std::vector<uint8_t>* out_policy_blob);
  void StorePolicyForUser(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
      const std::string& in_account_id,
      const std::vector<uint8_t>& in_policy_blob);
  void StoreUnsignedPolicyForUser(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
      const std::string& in_account_id,
      const std::vector<uint8_t>& in_policy_blob);
  bool RetrievePolicyForUser(
      brillo::ErrorPtr* error,
      const std::string& in_account_id,
      std::vector<uint8_t>* out_policy_blob);
  void StoreDeviceLocalAccountPolicy(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response,
      const std::string& in_account_id,
      const std::vector<uint8_t>& in_policy_blob);
  bool RetrieveDeviceLocalAccountPolicy(
      brillo::ErrorPtr* error,
      const std::string& in_account_id,
      std::vector<uint8_t>* out_policy_blob);

  std::string RetrieveSessionState();
  std::map<std::string, std::string> RetrieveActiveSessions();

  void HandleSupervisedUserCreationStarting();
  void HandleSupervisedUserCreationFinished();

  bool LockScreen(brillo::ErrorPtr* error);
  void HandleLockScreenShown();
  void HandleLockScreenDismissed();

  bool RestartJob(brillo::ErrorPtr* error,
                  const dbus::FileDescriptor& in_cred_fd,
                  const std::vector<std::string>& in_argv);

  bool StartDeviceWipe(brillo::ErrorPtr* error);
  void SetFlagsForUser(const std::string& in_account_id,
                       const std::vector<std::string>& in_flags);

  void GetServerBackedStateKeys(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          std::vector<std::vector<uint8_t>>>> response);

  bool InitMachineInfo(brillo::ErrorPtr* error, const std::string& in_data);
  bool StartContainer(brillo::ErrorPtr* error, const std::string& in_name);
  bool StopContainer(brillo::ErrorPtr* error, const std::string& in_name);

  bool StartArcInstance(brillo::ErrorPtr* error,
                        const std::vector<uint8_t>& in_request,
                        std::string* out_container_instance_id);
  bool StopArcInstance(brillo::ErrorPtr* error);
  bool SetArcCpuRestriction(brillo::ErrorPtr* error,
                            uint32_t in_restriction_state);
  bool EmitArcBooted(brillo::ErrorPtr* error,
                     const std::string& in_account_id);
  bool GetArcStartTimeTicks(brillo::ErrorPtr* error,
                            int64_t* out_start_time);
  bool RemoveArcData(brillo::ErrorPtr* error,
                     const std::string& in_account_id);

  // PolicyService::Delegate implementation:
  void OnPolicyPersisted(bool success) override;
  void OnKeyPersisted(bool success) override;

  // KeyGenerator::Delegate implementation:
  void OnKeyGenerated(const std::string& username,
                      const base::FilePath& temp_key_file) override;

 private:
  // Holds the state related to one of the signed in users.
  struct UserSession;

  using UserSessionMap = std::map<std::string, std::unique_ptr<UserSession>>;

  // Called when the tlsdated service becomes initially available.
  void OnSystemClockServiceAvailable(bool service_available);

  // Request the LastSyncInfo from tlsdated daemon.
  void GetSystemClockLastSyncInfo();

  // The response to LastSyncInfo request is processed here. If the time sync
  // was done then the state keys are generated, otherwise another LastSyncInfo
  // request is scheduled to be done later.
  void OnGotSystemClockLastSyncInfo(dbus::Response* response);

  // Given a policy key stored at temp_key_file, pulls it off disk,
  // validates that it is a correctly formed key pair, and ensures it is
  // stored for the future in the provided user's NSSDB.
  void ImportValidateAndStoreGeneratedKey(const std::string& username,
                                          const base::FilePath& temp_key_file);

  // Normalizes an account ID in the case of a legacy email address.
  // Returns true on success, false otherwise. In case of an error,
  // brillo::Error instance is set to |error_out|.
  static bool NormalizeAccountId(const std::string& account_id,
                                 std::string* actual_account_id_out,
                                 brillo::ErrorPtr* error_out);

  bool AllSessionsAreIncognito();

  std::unique_ptr<UserSession> CreateUserSession(const std::string& username,
                                                 bool is_incognito,
                                                 brillo::ErrorPtr* error);

  // Verifies whether unsigned policies are permitted to be stored.
  // Returns nullptr on success. Otherwise, an error that should be used in a
  // reply to the D-Bus method call is returned.
  brillo::ErrorPtr VerifyUnsignedPolicyStore();

  PolicyService* GetPolicyService(const std::string& account_id);

  // Shared implementation of StorePolicy() and StoreUnsignedPolicy().
  void StorePolicyInternal(
      const std::vector<uint8_t>& policy_blob,
      SignatureCheck signature_check,
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response);

  // Shared implementation of StorePolicyForUser() and
  // StoreUnsignedPolicyForUser().
  void StorePolicyForUserInternal(
      const std::string& account_id,
      const std::vector<uint8_t>& policy_blob,
      SignatureCheck signature_check,
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<>> response);

#if USE_CHEETS
  // Implementation of StartArcInstance, except parsing blob to protobuf.
  bool StartArcInstanceInternal(brillo::ErrorPtr* error,
                                const StartArcInstanceRequest& in_request,
                                std::string* out_container_instance_id);

  // Starts the Android container for ARC. If the container has started,
  // container_instance_id will be returned. Otherwise, an empty string
  // is returned and brillo::Error instance is set to |error_out|.
  // After this succeeds, in case of ARC stop, OnAndroidContainerStopped()
  // is called with the returned container_instance_id.
  std::string StartArcContainer(const std::string& init_signal,
                                const std::vector<std::string>& init_keyvals,
                                brillo::ErrorPtr* error_out);

  // Starts the network interface for the Android container for ARC.
  // On success, returns true. Otherwise returns false and brillo::Error
  // instance is set to |error_out|.
  bool StartArcNetwork(brillo::ErrorPtr* error_out);

  // Called when the Android container is stopped.
  void OnAndroidContainerStopped(const std::string& container_instance_id,
                                 pid_t pid,
                                 bool clean);

  // Renames android-data/ in the user's home directory to android-data-old/,
  // then recursively removes the renamed directory. Returns false when it
  // fails to rename android-data/.
  bool RemoveArcDataInternal(const base::FilePath& android_data_dir,
                             const base::FilePath& android_data_old_dir);
#endif

  bool session_started_;
  bool session_stopping_;
  bool screen_locked_;
  bool supervised_user_creation_ongoing_;
  bool system_clock_synchronized_;
  std::string cookie_;

  base::FilePath chrome_testing_path_;

  std::unique_ptr<InitDaemonController> init_controller_;

  base::Closure lock_screen_closure_;
  base::Closure restart_device_closure_;
  base::Closure start_arc_instance_closure_;
  base::Closure stop_arc_instance_closure_;

  base::TimeTicks arc_start_time_;

  DBusSignalEmitterInterface* dbus_emitter_;            // Owned by the caller.
  KeyGenerator* key_gen_;                               // Owned by the caller.
  ServerBackedStateKeyGenerator* state_key_generator_;  // Owned by the caller.
  ProcessManagerServiceInterface* manager_;             // Owned by the caller.
  LoginMetrics* login_metrics_;                         // Owned by the caller.
  NssUtil* nss_;                                        // Owned by the caller.
  SystemUtils* system_;                                 // Owned by the caller.
  Crossystem* crossystem_;                              // Owned by the caller.
  VpdProcess* vpd_process_;                             // Owned by the caller.
  PolicyKey* owner_key_;                                // Owned by the caller.
  ContainerManagerInterface* android_container_;        // Owned by the caller.
  InstallAttributesReader* install_attributes_reader_;  // Owned by the caller.
  dbus::ObjectProxy* system_clock_proxy_;               // Owned by the caller.

  std::unique_ptr<DevicePolicyService> device_policy_;
  std::unique_ptr<UserPolicyServiceFactory> user_policy_factory_;
  std::unique_ptr<DeviceLocalAccountPolicyService> device_local_account_policy_;
  RegenMitigator mitigator_;

  // Callbacks passed to RequestServerBackedStateKeys() while
  // |system_clock_synchrononized_| was false. They will be run by
  // OnGotSystemClockLastSyncInfo() once the clock is synchronized.
  std::vector<ServerBackedStateKeyGenerator::StateKeyCallback>
      pending_state_key_callbacks_;

  // Map of the currently signed-in users to their state.
  UserSessionMap user_sessions_;

  base::WeakPtrFactory<SessionManagerImpl> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(SessionManagerImpl);
};

}  // namespace login_manager
#endif  // LOGIN_MANAGER_SESSION_MANAGER_IMPL_H_
