// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_AUTH_STACK_MANAGER_H_
#define BIOD_AUTH_STACK_MANAGER_H_

#include <string>

#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <brillo/secure_blob.h>
#include <chromeos/dbus/service_constants.h>

#include "biod/biometrics_manager.h"
#include "biod/proto_bindings/constants.pb.h"
#include "biod/proto_bindings/messages.pb.h"

namespace biod {

// An AuthStackManager object represents one biometric input device and all of
// the records registered with it. At a high level, there are 3 operations that
// are supported: 1) enrolling new record objects, 2) authenticating against
// those record objects, and 3) destroying individual record objects made from
// this AuthStackManager. For enroll and authenticate operations, the
// AuthStackManager object must be entered into AuthSession or EnrollSession,
// which is represented in code by the return of the session objects.
// EnrollSession and AuthSession can be thought of as session objects that are
// ongoing as long as the unique pointers remain in scope and the End/Cancel
// methods haven't been called. It's undefined what StartEnrollSession or
// StartAuthSession will do if there is an valid outstanding EnrollSession or
// AuthSession object in the wild. The actual enroll and authenticate of the
// records comes *after* those session ends: the manager will be put in a ready
// state for those operations, and CreateCredential/AuthenticateCredential can
// be called.
class AuthStackManager {
 public:
  using Session = base::ScopedClosureRunner;

  virtual ~AuthStackManager() = default;
  virtual BiometricType GetType() = 0;

  // Puts this AuthStackManager into EnrollSession mode, which can be ended by
  // letting the returned session fall out of scope. This will fail if ANY
  // other mode is active. Returns a false Session on failure. A
  // successful enroll session will put the AuthStackManager into ready state
  // for CreateCredential.
  virtual Session StartEnrollSession() = 0;

  // Creates the actual fingerprint record. Should only be called after an
  // enroll session completes successfully. See CreateCredentialRequest/Reply
  // protos for the detailed function signature.
  virtual CreateCredentialReply CreateCredential(
      const CreateCredentialRequest& request) = 0;

  // Puts this AuthStackManager into AuthSession mode, which can be ended by
  // letting the returned session fall out of scope. This will fail if ANY other
  // mode is active. Returns a false Session on failure. A successful auth
  // session will put the AuthStackManager into ready state for
  // AuthenticateCredential.
  virtual Session StartAuthSession(std::string user_id) = 0;

  using AuthenticateCredentialCallback =
      base::OnceCallback<void(AuthenticateCredentialReply)>;
  // Loads the fingerprint records and perform the fingerprint match. Should
  // only be called after an auth session completes successfully. See
  // AuthenticateCredentialRequest/Reply protos for the detailed function
  // signature.
  virtual void AuthenticateCredential(
      const AuthenticateCredentialRequest& request,
      AuthenticateCredentialCallback callback) = 0;

  virtual DeleteCredentialReply DeleteCredential(
      const DeleteCredentialRequest& request) = 0;

  // This will be called when a user logs out.
  virtual void OnUserLoggedOut() = 0;

  // This will be called when a user logs in.
  virtual void OnUserLoggedIn(const std::string& user_id) = 0;

  // The callbacks should remain valid as long as this object is valid.

  // This is a repeating callback because it is set by the AuthStack dbus
  // wrapper, which registers to this callback once and emit a dbus signal on
  // every enroll scan done.
  using EnrollStatus = BiometricsManager::EnrollStatus;
  using EnrollScanDoneCallback = base::RepeatingCallback<void(
      ScanResult, const EnrollStatus&, brillo::Blob auth_nonce)>;
  virtual void SetEnrollScanDoneHandler(
      const EnrollScanDoneCallback& on_enroll_scan_done) = 0;

  // This is a repeating callback because it is set by the AuthStack dbus
  // wrapper, which registers to this callback once and emit a dbus signal on
  // every auth scan done.
  using AuthScanDoneCallback =
      base::RepeatingCallback<void(brillo::Blob auth_nonce)>;
  virtual void SetAuthScanDoneHandler(
      const AuthScanDoneCallback& on_auth_scan_done) = 0;

  // Invoked during any session to indicate that the session has ended with
  // failure. Any EnrollSession record that was underway is thrown away and
  // AuthSession will no longer be happening.
  using SessionFailedCallback = base::RepeatingCallback<void()>;
  virtual void SetSessionFailedHandler(
      const SessionFailedCallback& on_session_failed) = 0;

 protected:
  virtual void EndEnrollSession() = 0;
  virtual void EndAuthSession() = 0;
};
}  // namespace biod

#endif  // BIOD_AUTH_STACK_MANAGER_H_
