// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_AUTH_STACK_MANAGER_WRAPPER_H_
#define BIOD_AUTH_STACK_MANAGER_WRAPPER_H_

#include <memory>
#include <string>
#include <vector>

#include <brillo/dbus/exported_object_manager.h>
#include <dbus/message.h>
#include <dbus/object_path.h>

#include "biod/auth_stack_manager.h"
#include "biod/session_state_manager.h"

namespace biod {

// Wrapper of the given AuthStackManager, which actually implements the dbus
// service that exposes the AuthStack APIs.
class AuthStackManagerWrapper : public SessionStateManagerInterface::Observer {
 public:
  AuthStackManagerWrapper(
      std::unique_ptr<AuthStackManager> auth_stack_manager,
      brillo::dbus_utils::ExportedObjectManager* object_manager,
      SessionStateManagerInterface* session_state_manager,
      dbus::ObjectPath object_path,
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction
          completion_callback);
  AuthStackManagerWrapper(const AuthStackManagerWrapper&) = delete;
  AuthStackManagerWrapper& operator=(const AuthStackManagerWrapper&) = delete;
  ~AuthStackManagerWrapper() override;

  // SessionStateManagerInterface::Observer
  void OnUserLoggedIn(const std::string& sanitized_username,
                      bool is_new_login) override;
  void OnUserLoggedOut() override;
  void OnSessionResumedFromHibernate() override;

 private:
  void FinalizeEnrollSessionObject();
  void FinalizeAuthSessionObject();
  void OnNameOwnerChanged(dbus::Signal* signal);
  void OnEnrollScanDone(ScanResult scan_result,
                        const AuthStackManager::EnrollStatus& enroll_status);
  void OnAuthScanDone();
  void OnSessionFailed();
  void GetNonce(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                    const GetNonceReply&>> response);
  bool StartEnrollSession(brillo::ErrorPtr* error,
                          dbus::Message* message,
                          const StartEnrollSessionRequest& request,
                          dbus::ObjectPath* enroll_session_path);
  void CreateCredential(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                            const CreateCredentialReply&>> response,
                        const CreateCredentialRequestV2& request);
  bool StartAuthSession(brillo::ErrorPtr* error,
                        dbus::Message* message,
                        const StartAuthSessionRequest& request,
                        dbus::ObjectPath* auth_session_path);
  void AuthenticateCredential(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          const AuthenticateCredentialReply&>> response,
      const AuthenticateCredentialRequestV2& request);
  void DeleteCredential(std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
                            const DeleteCredentialReply&>> response,
                        const DeleteCredentialRequest& request);
  bool EnrollSessionCancel(brillo::ErrorPtr* error);
  bool AuthSessionEnd(brillo::ErrorPtr* error);

  std::unique_ptr<AuthStackManager> auth_stack_manager_;
  SessionStateManagerInterface* session_state_manager_;

  brillo::dbus_utils::DBusObject dbus_object_;
  dbus::ObjectPath object_path_;
  brillo::dbus_utils::ExportedProperty<uint32_t> property_type_;

  AuthStackManager::Session enroll_session_;
  std::string enroll_session_owner_;
  dbus::ObjectPath enroll_session_object_path_;
  std::unique_ptr<brillo::dbus_utils::DBusObject> enroll_session_dbus_object_;

  AuthStackManager::Session auth_session_;
  std::string auth_session_owner_;
  dbus::ObjectPath auth_session_object_path_;
  std::unique_ptr<brillo::dbus_utils::DBusObject> auth_session_dbus_object_;
};

}  // namespace biod
#endif  // BIOD_AUTH_STACK_MANAGER_WRAPPER_H_
