// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_CROS_FP_AUTH_STACK_MANAGER_H_
#define BIOD_CROS_FP_AUTH_STACK_MANAGER_H_

#include "biod/auth_stack_manager.h"

#include <memory>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/timer/timer.h>
#include <libhwsec/frontend/pinweaver/frontend.h>

#include "biod/cros_fp_device.h"
#include "biod/cros_fp_session_manager.h"
#include "biod/maintenance_scheduler.h"
#include "biod/pairing_key_storage.h"
#include "biod/power_button_filter_interface.h"
#include "biod/proto_bindings/constants.pb.h"
#include "biod/proto_bindings/messages.pb.h"

namespace biod {

class BiodMetrics;

class CrosFpAuthStackManager : public AuthStackManager {
 public:
  // Current state of CrosFpAuthStackManager. We maintain a state machine
  // because some operations can only be processed in some states.
  enum class State {
    // Initial state, neither any session is pending nor we're expecting
    // Create/AuthenticateCredential commands to come.
    kNone,
    // An EnrollSession is ongoing.
    kEnroll,
    // An EnrollSession is completed successfully and we're expecting a
    // CreateCredential command.
    kEnrollDone,
    // An AuthSession is ongoing.
    kAuth,
    // An AuthSession is completed successfully and we're expecting a
    // AuthenticateCredential command.
    kAuthDone,
    // An asynchronous matching operation is ongoing.
    kMatch,
    // An AuthenticateCredential is completed, and we're waiting for user to
    // lift their finger before the next auth attempt.
    kWaitForFingerUp,
    // If during WaitForFingerUp state, we received an AuthenticateCredential
    // command, we transition to this state so that after finger is up we
    // immediately start to detect finger down event.
    kAuthWaitForFingerUp,
    // Something went wrong in keeping sync between biod and FPMCU, and it's
    // better to not process any Enroll/Auth commands in this state.
    kLocked,
  };

  CrosFpAuthStackManager(
      std::unique_ptr<PowerButtonFilterInterface> power_button_filter,
      std::unique_ptr<ec::CrosFpDeviceInterface> cros_fp_device,
      BiodMetricsInterface* biod_metrics,
      std::unique_ptr<CrosFpSessionManager> session_manager,
      std::unique_ptr<PairingKeyStorage> pk_storage,
      std::unique_ptr<const hwsec::PinWeaverFrontend> pinweaver,
      State state = State::kNone);
  CrosFpAuthStackManager(const CrosFpAuthStackManager&) = delete;
  CrosFpAuthStackManager& operator=(const CrosFpAuthStackManager&) = delete;

  // Initializes the AuthStack. Without calling Initialize, many functions might
  // not work.
  bool Initialize();
  // Establishes Pk with GSC.
  virtual bool EstablishPairingKey();

  // AuthStackManager overrides:
  ~CrosFpAuthStackManager() override = default;

  BiometricType GetType() override;
  AuthStackManager::Session StartEnrollSession() override;
  CreateCredentialReply CreateCredential(
      const CreateCredentialRequest& request) override;
  AuthStackManager::Session StartAuthSession(std::string user_id) override;
  void AuthenticateCredential(
      const AuthenticateCredentialRequest& request,
      AuthStackManager::AuthenticateCredentialCallback callback) override;
  DeleteCredentialReply DeleteCredential(
      const DeleteCredentialRequest& request) override;
  void OnUserLoggedOut() override;
  void OnUserLoggedIn(const std::string& user_id) override;
  void SetEnrollScanDoneHandler(const AuthStackManager::EnrollScanDoneCallback&
                                    on_enroll_scan_done) override;
  void SetAuthScanDoneHandler(
      const AuthStackManager::AuthScanDoneCallback& on_auth_scan_done) override;
  void SetSessionFailedHandler(const AuthStackManager::SessionFailedCallback&
                                   on_session_failed) override;

  State GetState() const { return state_; }

 protected:
  void EndEnrollSession() override;
  void EndAuthSession() override;

 private:
  using SessionAction = base::RepeatingCallback<void(const uint32_t event)>;

  // For testing.
  friend class CrosFpAuthStackManagerPeer;

  void OnMkbpEvent(uint32_t event);
  void KillMcuSession();
  void OnTaskComplete();

  // Load the pairing key into FPMCU. This is called on every boot when
  // AuthStackManager is initialized.
  bool LoadPairingKey();

  void OnEnrollScanDone(ScanResult result,
                        const AuthStackManager::EnrollStatus& enroll_status,
                        brillo::Blob auth_nonce);
  void OnAuthScanDone(brillo::Blob auth_nonce);
  void OnSessionFailed();

  bool LoadUser(std::string user_id, bool lock_to_user);
  // Preload encrypted user templates into FPMCU. We only need to do this when
  // the current user has changed, or when we delete a template and a reload is
  // required.
  bool PreloadCurrentUserTemplates();

  bool RequestEnrollImage();
  bool RequestEnrollFingerUp();
  void DoEnrollImageEvent(uint32_t event);
  void DoEnrollFingerUpEvent(uint32_t event);
  bool RequestMatchFingerDown();
  void OnMatchFingerDown(uint32_t event);
  void DoMatch(const AuthenticateCredentialRequest& request,
               AuthStackManager::AuthenticateCredentialCallback callback);
  void DoMatchEvent(
      AuthenticateCredentialRequest request,
      std::shared_ptr<AuthStackManager::AuthenticateCredentialCallback>
          callback,
      uint32_t event);
  void AbortDoMatch(AuthStackManager::AuthenticateCredentialCallback callback);
  bool RequestFingerUp();
  void OnFingerUpEvent(uint32_t event);

  std::string CurrentStateToString();
  // Whether current state is waiting for a next session action.
  bool IsActiveState();
  bool CanStartEnroll();
  bool CanCreateCredential();
  bool CanStartAuth();
  bool CanAuthenticateCredential();

  BiodMetricsInterface* biod_metrics_ = nullptr;
  std::unique_ptr<ec::CrosFpDeviceInterface> cros_dev_;

  SessionAction next_session_action_;

  AuthStackManager::EnrollScanDoneCallback on_enroll_scan_done_;
  AuthStackManager::AuthScanDoneCallback on_auth_scan_done_;
  AuthStackManager::SessionFailedCallback on_session_failed_;

  // A timer that aborts the match session when time is up.
  base::OneShotTimer do_match_timer_;

  std::unique_ptr<PowerButtonFilterInterface> power_button_filter_;

  std::unique_ptr<CrosFpSessionManager> session_manager_;

  std::unique_ptr<PairingKeyStorage> pk_storage_;

  std::unique_ptr<const hwsec::PinWeaverFrontend> pinweaver_;

  State state_;

  // This is used to disallow authenticating/enrolling fingerprint for a second
  // user after a user has logged-in. Note that CrOS currently supports
  // multi-login, but as biod and FPMCU can only hold state for a single user,
  // we stick to the first logged-in user.
  bool locked_to_current_user_ = false;

  std::unique_ptr<MaintenanceScheduler> maintenance_scheduler_;

  base::WeakPtrFactory<CrosFpAuthStackManager> session_weak_factory_;
};

}  // namespace biod

#endif  // BIOD_CROS_FP_AUTH_STACK_MANAGER_H_
