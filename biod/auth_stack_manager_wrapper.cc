// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/auth_stack_manager_wrapper.h"

#include <algorithm>
#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <dbus/object_proxy.h>

#include "biod/utils.h"

namespace biod {

using brillo::dbus_utils::AsyncEventSequencer;
using brillo::dbus_utils::DBusInterface;
using brillo::dbus_utils::DBusMethodResponse;
using brillo::dbus_utils::DBusObject;
using brillo::dbus_utils::ExportedObjectManager;
using dbus::ObjectPath;

namespace errors {
const char kDomain[] = "biod";
const char kInternalError[] = "internal_error";
const char kInvalidArguments[] = "invalid_arguments";
}  // namespace errors

AuthStackManagerWrapper::AuthStackManagerWrapper(
    std::unique_ptr<AuthStackManager> auth_stack_manager,
    ExportedObjectManager* object_manager,
    SessionStateManagerInterface* session_state_manager,
    ObjectPath object_path,
    AsyncEventSequencer::CompletionAction completion_callback)
    : auth_stack_manager_(std::move(auth_stack_manager)),
      session_state_manager_(session_state_manager),
      dbus_object_(object_manager, object_manager->GetBus(), object_path),
      object_path_(std::move(object_path)),
      enroll_session_object_path_(object_path_.value() + "/EnrollSession"),
      auth_session_object_path_(object_path_.value() + "/AuthSession") {
  CHECK(auth_stack_manager_);

  auth_stack_manager_->SetEnrollScanDoneHandler(base::BindRepeating(
      &AuthStackManagerWrapper::OnEnrollScanDone, base::Unretained(this)));
  auth_stack_manager_->SetAuthScanDoneHandler(base::BindRepeating(
      &AuthStackManagerWrapper::OnAuthScanDone, base::Unretained(this)));
  auth_stack_manager_->SetSessionFailedHandler(base::BindRepeating(
      &AuthStackManagerWrapper::OnSessionFailed, base::Unretained(this)));

  dbus::ObjectProxy* bus_proxy = object_manager->GetBus()->GetObjectProxy(
      dbus::kDBusServiceName, dbus::ObjectPath(dbus::kDBusServicePath));
  bus_proxy->ConnectToSignal(
      dbus::kDBusInterface, "NameOwnerChanged",
      base::BindRepeating(&AuthStackManagerWrapper::OnNameOwnerChanged,
                          base::Unretained(this)),
      base::BindOnce(&LogOnSignalConnected));

  DBusInterface* auth_stack_interface =
      dbus_object_.AddOrGetInterface(kAuthStackManagerInterface);
  property_type_.SetValue(
      static_cast<uint32_t>(auth_stack_manager_->GetType()));
  auth_stack_interface->AddProperty(kBiometricsManagerBiometricTypeProperty,
                                    &property_type_);
  auth_stack_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      kAuthStackManagerStartEnrollSessionMethod,
      base::BindRepeating(&AuthStackManagerWrapper::StartEnrollSession,
                          base::Unretained(this)));
  auth_stack_interface->AddSimpleMethodHandlerWithErrorAndMessage(
      kAuthStackManagerStartAuthSessionMethod,
      base::BindRepeating(&AuthStackManagerWrapper::StartAuthSession,
                          base::Unretained(this)));
  auth_stack_interface->AddMethodHandler(
      kAuthStackManagerCreateCredentialMethod, base::Unretained(this),
      &AuthStackManagerWrapper::CreateCredential);
  auth_stack_interface->AddMethodHandler(
      kAuthStackManagerAuthenticateCredentialMethod, base::Unretained(this),
      &AuthStackManagerWrapper::AuthenticateCredential);
  dbus_object_.RegisterAsync(std::move(completion_callback));

  // Add this AuthStackManagerWrapper instance to observe session state
  // changes.
  session_state_manager_->AddObserver(this);
}

AuthStackManagerWrapper::~AuthStackManagerWrapper() {
  session_state_manager_->RemoveObserver(this);
}

void AuthStackManagerWrapper::FinalizeEnrollSessionObject() {
  enroll_session_owner_.clear();
  enroll_session_dbus_object_->UnregisterAndBlock();
  enroll_session_dbus_object_.reset();
}

void AuthStackManagerWrapper::FinalizeAuthSessionObject() {
  auth_session_owner_.clear();
  auth_session_dbus_object_->UnregisterAndBlock();
  auth_session_dbus_object_.reset();
}

void AuthStackManagerWrapper::OnNameOwnerChanged(dbus::Signal* sig) {
  dbus::MessageReader reader(sig);
  std::string name, old_owner, new_owner;
  if (!reader.PopString(&name) || !reader.PopString(&old_owner) ||
      !reader.PopString(&new_owner)) {
    LOG(ERROR) << "Received invalid NameOwnerChanged signal";
    return;
  }

  // We are only interested in cases where a name gets dropped from D-Bus.
  if (name.empty() || !new_owner.empty())
    return;

  // If one of the session was owned by the dropped name, the session should
  // also be dropped, as there is nobody left to end it explicitly.

  if (name == enroll_session_owner_) {
    LOG(INFO) << "EnrollSession object owner " << enroll_session_owner_
              << " has died. EnrollSession is canceled automatically.";
    if (enroll_session_)
      enroll_session_.RunAndReset();

    if (enroll_session_dbus_object_)
      FinalizeEnrollSessionObject();
  }

  if (name == auth_session_owner_) {
    LOG(INFO) << "AuthSession object owner " << auth_session_owner_
              << " has died. AuthSession is ended automatically.";
    if (auth_session_)
      auth_session_.RunAndReset();

    if (auth_session_dbus_object_)
      FinalizeAuthSessionObject();
  }
}

void AuthStackManagerWrapper::OnEnrollScanDone(
    ScanResult scan_result,
    const AuthStackManager::EnrollStatus& enroll_status,
    brillo::Blob auth_nonce) {
  if (!enroll_session_dbus_object_)
    return;

  dbus::Signal enroll_scan_done_signal(kAuthStackManagerInterface,
                                       kBiometricsManagerEnrollScanDoneSignal);
  dbus::MessageWriter writer(&enroll_scan_done_signal);
  EnrollScanDone proto;
  proto.set_scan_result(scan_result);
  proto.set_done(enroll_status.done);
  proto.set_auth_nonce(brillo::BlobToString(auth_nonce));
  if (enroll_status.percent_complete >= 0) {
    proto.set_percent_complete(enroll_status.percent_complete);
  }
  writer.AppendProtoAsArrayOfBytes(proto);
  dbus_object_.SendSignal(&enroll_scan_done_signal);
  if (enroll_status.done) {
    enroll_session_.RunAndReset();
    FinalizeEnrollSessionObject();
  }
}

void AuthStackManagerWrapper::OnAuthScanDone(brillo::Blob auth_nonce) {
  if (!auth_session_dbus_object_)
    return;

  dbus::Signal auth_scan_done_signal(kAuthStackManagerInterface,
                                     kBiometricsManagerAuthScanDoneSignal);
  dbus::MessageWriter writer(&auth_scan_done_signal);
  AuthScanDone proto;
  proto.set_auth_nonce(brillo::BlobToString(auth_nonce));
  writer.AppendProtoAsArrayOfBytes(proto);
  dbus_object_.SendSignal(&auth_scan_done_signal);
}

void AuthStackManagerWrapper::OnSessionFailed() {
  if (enroll_session_dbus_object_) {
    dbus::Signal session_failed_signal(kAuthStackManagerInterface,
                                       kBiometricsManagerSessionFailedSignal);
    dbus_object_.SendSignal(&session_failed_signal);
    FinalizeEnrollSessionObject();
  }

  if (enroll_session_)
    enroll_session_.RunAndReset();

  if (auth_session_dbus_object_) {
    dbus::Signal session_failed_signal(kAuthStackManagerInterface,
                                       kBiometricsManagerSessionFailedSignal);
    dbus_object_.SendSignal(&session_failed_signal);
    FinalizeAuthSessionObject();
  }

  if (auth_session_)
    auth_session_.RunAndReset();
}

bool AuthStackManagerWrapper::StartEnrollSession(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    ObjectPath* enroll_session_path) {
  AuthStackManager::Session enroll_session =
      auth_stack_manager_->StartEnrollSession();
  if (!enroll_session) {
    *error = brillo::Error::Create(FROM_HERE, errors::kDomain,
                                   errors::kInternalError,
                                   "Failed to start EnrollSession");
    return false;
  }
  enroll_session_ = std::move(enroll_session);

  enroll_session_dbus_object_ = std::make_unique<DBusObject>(
      nullptr, dbus_object_.GetBus(), enroll_session_object_path_);
  DBusInterface* enroll_session_interface =
      enroll_session_dbus_object_->AddOrGetInterface(kEnrollSessionInterface);
  enroll_session_interface->AddSimpleMethodHandlerWithError(
      kEnrollSessionCancelMethod,
      base::BindRepeating(&AuthStackManagerWrapper::EnrollSessionCancel,
                          base::Unretained(this)));
  enroll_session_dbus_object_->RegisterAndBlock();
  *enroll_session_path = enroll_session_object_path_;
  enroll_session_owner_ = message->GetSender();

  return true;
}

void AuthStackManagerWrapper::CreateCredential(
    std::unique_ptr<DBusMethodResponse<const CreateCredentialReply&>> response,
    const CreateCredentialRequest& request) {
  response->Return(auth_stack_manager_->CreateCredential(request));
}

bool AuthStackManagerWrapper::StartAuthSession(brillo::ErrorPtr* error,
                                               dbus::Message* message,
                                               std::string user_id,
                                               ObjectPath* auth_session_path) {
  AuthStackManager::Session auth_session =
      auth_stack_manager_->StartAuthSession(std::move(user_id));
  if (!auth_session) {
    *error = brillo::Error::Create(FROM_HERE, errors::kDomain,
                                   errors::kInternalError,
                                   "Failed to start AuthSession");
    return false;
  }
  auth_session_ = std::move(auth_session);

  auth_session_dbus_object_ = std::make_unique<DBusObject>(
      nullptr, dbus_object_.GetBus(), auth_session_object_path_);
  DBusInterface* auth_session_interface =
      auth_session_dbus_object_->AddOrGetInterface(kAuthSessionInterface);
  auth_session_interface->AddSimpleMethodHandlerWithError(
      kAuthSessionEndMethod,
      base::BindRepeating(&AuthStackManagerWrapper::AuthSessionEnd,
                          base::Unretained(this)));
  auth_session_dbus_object_->RegisterAndBlock();
  *auth_session_path = auth_session_object_path_;
  auth_session_owner_ = message->GetSender();

  return true;
}

void AuthStackManagerWrapper::AuthenticateCredential(
    std::unique_ptr<DBusMethodResponse<const AuthenticateCredentialReply&>>
        response,
    const AuthenticateCredentialRequest& request) {
  response->Return(auth_stack_manager_->AuthenticateCredential(request));
}

bool AuthStackManagerWrapper::EnrollSessionCancel(brillo::ErrorPtr* error) {
  if (!enroll_session_) {
    LOG(WARNING) << "DBus client attempted to cancel null EnrollSession";
    *error = brillo::Error::Create(FROM_HERE, errors::kDomain,
                                   errors::kInvalidArguments,
                                   "EnrollSession object was null");
    return false;
  }
  enroll_session_.RunAndReset();

  if (enroll_session_dbus_object_) {
    FinalizeEnrollSessionObject();
  }
  return true;
}

bool AuthStackManagerWrapper::AuthSessionEnd(brillo::ErrorPtr* error) {
  if (!auth_session_) {
    LOG(WARNING) << "DBus client attempted to cancel null AuthSession";
    *error = brillo::Error::Create(FROM_HERE, errors::kDomain,
                                   errors::kInvalidArguments,
                                   "AuthSession object was null");
    return false;
  }
  auth_session_.RunAndReset();

  if (auth_session_dbus_object_) {
    FinalizeAuthSessionObject();
  }
  return true;
}

void AuthStackManagerWrapper::OnUserLoggedIn(
    const std::string& sanitized_username, bool is_new_login) {
  auth_stack_manager_->OnUserLoggedIn(sanitized_username);
}

void AuthStackManagerWrapper::OnUserLoggedOut() {
  auth_stack_manager_->OnUserLoggedOut();
}

}  // namespace biod
