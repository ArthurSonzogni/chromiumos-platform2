// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "authpolicy/session_manager_client.h"

#include <memory>

#include <base/callback.h>
#include <base/logging.h>
#include <brillo/dbus/dbus_object.h>
#include <brillo/errors/error.h>
#include <dbus/login_manager/dbus-constants.h>
#include <session_manager/dbus-proxies.h>

namespace authpolicy {

namespace {

// Prints an error from a D-Bus method call. |method| is the name of the method.
// |error| is the Brillo-Error from parsing return values (can be nullptr).
void PrintError(const char* method, brillo::Error* error) {
  const char* error_msg =
      error ? error->GetMessage().c_str() : "Unknown error.";
  LOG(ERROR) << "Call to " << method << " failed. " << error_msg;
}

// Prints an error if connecting to a signal failed.
void LogOnSignalConnected(const std::string& interface_name,
                          const std::string& signal_name,
                          bool success) {
  if (!success) {
    LOG(ERROR) << "Failed to connect to signal " << signal_name
               << " of interface " << interface_name;
  }
}

}  // namespace

SessionManagerClient::SessionManagerClient(
    brillo::dbus_utils::DBusObject* dbus_object)
    : weak_ptr_factory_(this) {
  proxy_ = std::make_unique<org::chromium::SessionManagerInterfaceProxy>(
      dbus_object->GetBus());
}

SessionManagerClient::~SessionManagerClient() = default;

void SessionManagerClient::StoreUnsignedPolicyEx(
    const std::vector<uint8_t>& descriptor_blob,
    const std::vector<uint8_t>& policy_blob,
    const base::Callback<void(bool success)>& callback) {
  proxy_->StoreUnsignedPolicyExAsync(
      descriptor_blob, policy_blob,
      base::Bind(&SessionManagerClient::OnStorePolicySuccess,
                 weak_ptr_factory_.GetWeakPtr(), callback),
      base::Bind(&SessionManagerClient::OnStorePolicyError,
                 weak_ptr_factory_.GetWeakPtr(), callback));
}

bool SessionManagerClient::ListStoredComponentPolicies(
    const std::vector<uint8_t>& descriptor_blob,
    std::vector<std::string>* component_ids) {
  brillo::ErrorPtr error;
  if (!proxy_->ListStoredComponentPolicies(descriptor_blob, component_ids,
                                           &error)) {
    PrintError(login_manager::kSessionManagerListStoredComponentPolicies,
               error.get());
    return false;
  }
  return true;
}

void SessionManagerClient::ConnectToSessionStateChangedSignal(
    const base::Callback<void(const std::string& state)>& callback) {
  proxy_->RegisterSessionStateChangedSignalHandler(
      base::Bind(&SessionManagerClient::OnSessionStateChanged,
                 weak_ptr_factory_.GetWeakPtr(), callback),
      base::Bind(&LogOnSignalConnected));
}

std::string SessionManagerClient::RetrieveSessionState() {
  dbus::MethodCall method_call(
      login_manager::kSessionManagerInterface,
      login_manager::kSessionManagerRetrieveSessionState);
  std::string state;
  brillo::ErrorPtr error;
  if (!proxy_->RetrieveSessionState(&state, &error)) {
    PrintError(login_manager::kSessionManagerRetrieveSessionState, error.get());
    return std::string();
  }
  return state;
}

void SessionManagerClient::OnStorePolicySuccess(
    const base::Callback<void(bool success)>& callback) {
  callback.Run(true /* success */);
}

void SessionManagerClient::OnStorePolicyError(
    const base::Callback<void(bool success)>& callback, brillo::Error* error) {
  PrintError(login_manager::kSessionManagerStoreUnsignedPolicyEx, error);
  callback.Run(false /* success */);
}

void SessionManagerClient::OnSessionStateChanged(
    const base::Callback<void(const std::string& state)>& callback,
    const std::string& state) {
  callback.Run(state);
}

}  // namespace authpolicy
