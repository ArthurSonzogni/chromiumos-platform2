// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_BIOD_PROXY_MOCK_AUTH_STACK_MANAGER_PROXY_BASE_H_
#define BIOD_BIOD_PROXY_MOCK_AUTH_STACK_MANAGER_PROXY_BASE_H_

#include <string>

#include <gmock/gmock.h>

#include "biod/biod_proxy/auth_stack_manager_proxy_base.h"

namespace biod {
class MockAuthStackManagerProxyBase : public AuthStackManagerProxyBase {
 public:
  MockAuthStackManagerProxyBase() = default;
  ~MockAuthStackManagerProxyBase() override = default;

  MOCK_METHOD(void,
              ConnectToEnrollScanDoneSignal,
              (SignalCallback signal_callback,
               OnConnectedCallback on_connected_callback),
              (override));

  MOCK_METHOD(void,
              ConnectToAuthScanDoneSignal,
              (SignalCallback signal_callback,
               OnConnectedCallback on_connected_callback),
              (override));

  MOCK_METHOD(void,
              ConnectToSessionFailedSignal,
              (SignalCallback signal_callback,
               OnConnectedCallback on_connected_callback),
              (override));

  MOCK_METHOD(void,
              GetNonce,
              (base::OnceCallback<void(std::optional<GetNonceReply>)>),
              (override));

  MOCK_METHOD(void,
              StartEnrollSession,
              (const StartEnrollSessionRequest& request,
               base::OnceCallback<void(bool success)> callback),
              (override));

  MOCK_METHOD(void, EndEnrollSession, (), (override));

  MOCK_METHOD(void,
              CreateCredential,
              (const CreateCredentialRequest&,
               base::OnceCallback<void(std::optional<CreateCredentialReply>)>),
              (override));

  MOCK_METHOD(void,
              StartAuthSession,
              (const StartAuthSessionRequest& request,
               base::OnceCallback<void(bool success)> callback),
              (override));

  MOCK_METHOD(void, EndAuthSession, (), (override));

  MOCK_METHOD(
      void,
      AuthenticateCredential,
      (const AuthenticateCredentialRequest&,
       base::OnceCallback<void(std::optional<AuthenticateCredentialReply>)>),
      (override));

  MOCK_METHOD(void,
              DeleteCredential,
              (const DeleteCredentialRequest&,
               base::OnceCallback<void(std::optional<DeleteCredentialReply>)>),
              (override));

  MOCK_METHOD(void,
              EnrollLegacyTemplate,
              (const EnrollLegacyTemplateRequest&,
               base::OnceCallback<void(bool success)>),
              (override));

  MOCK_METHOD(void,
              ListLegacyRecords,
              (base::OnceCallback<void(std::optional<ListLegacyRecordsReply>)>),
              (override));
};
}  // namespace biod

#endif  // BIOD_BIOD_PROXY_MOCK_AUTH_STACK_MANAGER_PROXY_BASE_H_
