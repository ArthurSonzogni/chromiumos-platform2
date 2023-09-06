// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biod_proxy/auth_stack_manager_proxy_base.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <chromeos/dbus/service_constants.h>

#include "biod/biod_proxy/util.h"

namespace biod {

static const int kDbusTimeoutMs = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT;

using FinishCallback = base::RepeatingCallback<void(bool success)>;

std::unique_ptr<AuthStackManagerProxyBase> AuthStackManagerProxyBase::Create(
    const scoped_refptr<dbus::Bus>& bus, const dbus::ObjectPath& path) {
  auto auth_stack_manager_proxy_base =
      base::WrapUnique(new AuthStackManagerProxyBase());

  if (!auth_stack_manager_proxy_base->Initialize(bus, path))
    return nullptr;

  return auth_stack_manager_proxy_base;
}

void AuthStackManagerProxyBase::ConnectToEnrollScanDoneSignal(
    SignalCallback signal_callback, OnConnectedCallback on_connected_callback) {
  proxy_->ConnectToSignal(biod::kAuthStackManagerInterface,
                          biod::kBiometricsManagerEnrollScanDoneSignal,
                          std::move(signal_callback),
                          std::move(on_connected_callback));
}

void AuthStackManagerProxyBase::ConnectToAuthScanDoneSignal(
    SignalCallback signal_callback, OnConnectedCallback on_connected_callback) {
  proxy_->ConnectToSignal(biod::kAuthStackManagerInterface,
                          biod::kBiometricsManagerAuthScanDoneSignal,
                          std::move(signal_callback),
                          std::move(on_connected_callback));
}

void AuthStackManagerProxyBase::ConnectToSessionFailedSignal(
    SignalCallback signal_callback, OnConnectedCallback on_connected_callback) {
  proxy_->ConnectToSignal(biod::kAuthStackManagerInterface,
                          biod::kBiometricsManagerSessionFailedSignal,
                          std::move(signal_callback),
                          std::move(on_connected_callback));
}

void AuthStackManagerProxyBase::GetNonce(
    base::OnceCallback<void(std::optional<GetNonceReply>)> callback) {
  dbus::MethodCall method_call(biod::kAuthStackManagerInterface,
                               biod::kAuthStackManagerGetNonceMethod);
  proxy_->CallMethod(
      &method_call, kDbusTimeoutMs,
      base::BindOnce(&AuthStackManagerProxyBase::OnProtoResponse<GetNonceReply>,
                     base::Unretained(this), std::move(callback)));
}

void AuthStackManagerProxyBase::StartEnrollSession(
    const StartEnrollSessionRequest& request,
    base::OnceCallback<void(bool success)> callback) {
  dbus::MethodCall method_call(biod::kAuthStackManagerInterface,
                               biod::kAuthStackManagerStartEnrollSessionMethod);
  dbus::MessageWriter method_writer(&method_call);
  method_writer.AppendProtoAsArrayOfBytes(request);

  proxy_->CallMethod(
      &method_call, kDbusTimeoutMs,
      base::BindOnce(&AuthStackManagerProxyBase::OnStartEnrollSessionResponse,
                     base::Unretained(this), std::move(callback)));
}

void AuthStackManagerProxyBase::EndEnrollSession() {
  dbus::MethodCall end_call(biod::kEnrollSessionInterface,
                            biod::kEnrollSessionCancelMethod);
  (void)biod_enroll_session_->CallMethodAndBlock(&end_call, kDbusTimeoutMs);
}

void AuthStackManagerProxyBase::CreateCredential(
    const CreateCredentialRequestV2& request,
    base::OnceCallback<void(std::optional<CreateCredentialReply>)> callback) {
  dbus::MethodCall method_call(biod::kAuthStackManagerInterface,
                               biod::kAuthStackManagerCreateCredentialMethod);
  dbus::MessageWriter method_writer(&method_call);
  method_writer.AppendProtoAsArrayOfBytes(request);

  proxy_->CallMethod(
      &method_call, kDbusTimeoutMs,
      base::BindOnce(
          &AuthStackManagerProxyBase::OnProtoResponse<CreateCredentialReply>,
          base::Unretained(this), std::move(callback)));
}

void AuthStackManagerProxyBase::StartAuthSession(
    const StartAuthSessionRequest& request,
    base::OnceCallback<void(bool success)> callback) {
  dbus::MethodCall method_call(biod::kAuthStackManagerInterface,
                               biod::kAuthStackManagerStartAuthSessionMethod);
  dbus::MessageWriter method_writer(&method_call);
  method_writer.AppendProtoAsArrayOfBytes(request);

  proxy_->CallMethod(
      &method_call, kDbusTimeoutMs,
      base::BindOnce(&AuthStackManagerProxyBase::OnStartAuthSessionResponse,
                     base::Unretained(this), std::move(callback)));
}

void AuthStackManagerProxyBase::EndAuthSession() {
  dbus::MethodCall end_call(biod::kAuthSessionInterface,
                            biod::kAuthSessionEndMethod);
  (void)biod_auth_session_->CallMethodAndBlock(&end_call, kDbusTimeoutMs);
}

void AuthStackManagerProxyBase::AuthenticateCredential(
    const AuthenticateCredentialRequestV2& request,
    base::OnceCallback<void(std::optional<AuthenticateCredentialReply>)>
        callback) {
  dbus::MethodCall method_call(
      biod::kAuthStackManagerInterface,
      biod::kAuthStackManagerAuthenticateCredentialMethod);
  dbus::MessageWriter method_writer(&method_call);
  method_writer.AppendProtoAsArrayOfBytes(request);

  proxy_->CallMethod(
      &method_call, kDbusTimeoutMs,
      base::BindOnce(&AuthStackManagerProxyBase::OnProtoResponse<
                         AuthenticateCredentialReply>,
                     base::Unretained(this), std::move(callback)));
}

void AuthStackManagerProxyBase::DeleteCredential(
    const DeleteCredentialRequest& request,
    base::OnceCallback<void(std::optional<DeleteCredentialReply>)> callback) {
  dbus::MethodCall method_call(biod::kAuthStackManagerInterface,
                               biod::kAuthStackManagerDeleteCredentialMethod);
  dbus::MessageWriter method_writer(&method_call);
  method_writer.AppendProtoAsArrayOfBytes(request);

  proxy_->CallMethod(
      &method_call, kDbusTimeoutMs,
      base::BindOnce(
          &AuthStackManagerProxyBase::OnProtoResponse<DeleteCredentialReply>,
          base::Unretained(this), std::move(callback)));
}

bool AuthStackManagerProxyBase::Initialize(const scoped_refptr<dbus::Bus>& bus,
                                           const dbus::ObjectPath& path) {
  bus_ = bus;
  proxy_ = bus_->GetObjectProxy(biod::kBiodServiceName, path);

  if (!proxy_)
    return false;

  return true;
}

void AuthStackManagerProxyBase::OnStartEnrollSessionResponse(
    base::OnceCallback<void(bool success)> callback, dbus::Response* response) {
  biod_enroll_session_ = HandleStartSessionResponse(response);
  std::move(callback).Run(biod_enroll_session_ != nullptr);
}

void AuthStackManagerProxyBase::OnStartAuthSessionResponse(
    base::OnceCallback<void(bool success)> callback, dbus::Response* response) {
  biod_auth_session_ = HandleStartSessionResponse(response);
  std::move(callback).Run(biod_auth_session_ != nullptr);
}

dbus::ObjectProxy* AuthStackManagerProxyBase::HandleStartSessionResponse(
    dbus::Response* response) {
  if (!response) {
    LOG(ERROR) << "StartSession had no response.";
    return nullptr;
  }

  dbus::MessageReader response_reader(response);
  dbus::ObjectPath auth_path;
  if (!response_reader.PopObjectPath(&auth_path)) {
    LOG(ERROR) << "StartSession had incorrect response.";
    return nullptr;
  }
  return bus_->GetObjectProxy(biod::kBiodServiceName, auth_path);
}

}  // namespace biod
