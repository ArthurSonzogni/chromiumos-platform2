// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BIOD_MOCK_AUTH_STACK_MANAGER_H_
#define BIOD_MOCK_AUTH_STACK_MANAGER_H_

#include <memory>
#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "biod/auth_stack_manager.h"

namespace biod {

using testing::Return;

class MockAuthStackManager : public AuthStackManager {
 public:
  MockAuthStackManager() : session_weak_factory_(this) {}
  ~MockAuthStackManager() override = default;

  MOCK_METHOD(BiometricType, GetType, (), (override));
  MOCK_METHOD(GetNonceReply, GetNonce, (), (override));
  MOCK_METHOD(Session,
              StartEnrollSession,
              (const StartEnrollSessionRequest&),
              (override));
  MOCK_METHOD(CreateCredentialReply,
              CreateCredential,
              (const CreateCredentialRequestV2&),
              (override));
  MOCK_METHOD(Session,
              StartAuthSession,
              (const StartAuthSessionRequest&),
              (override));
  MOCK_METHOD(void,
              AuthenticateCredential,
              (const AuthenticateCredentialRequestV2&,
               AuthStackManager::AuthenticateCredentialCallback),
              (override));
  MOCK_METHOD(DeleteCredentialReply,
              DeleteCredential,
              (const DeleteCredentialRequest&),
              (override));
  MOCK_METHOD(void, OnUserLoggedOut, (), (override));
  MOCK_METHOD(void, OnUserLoggedIn, (const std::string&), (override));
  MOCK_METHOD(void,
              SetEnrollScanDoneHandler,
              (const EnrollScanDoneCallback& on_enroll_scan_done),
              (override));
  MOCK_METHOD(void,
              SetAuthScanDoneHandler,
              (const AuthScanDoneCallback& on_auth_scan_done),
              (override));
  MOCK_METHOD(void,
              SetSessionFailedHandler,
              (const SessionFailedCallback& on_session_failed),
              (override));
  MOCK_METHOD(void, EndEnrollSession, (), (override));
  MOCK_METHOD(void, EndAuthSession, (), (override));

  base::WeakPtrFactory<MockAuthStackManager> session_weak_factory_;
};

}  //  namespace biod

#endif  // BIOD_MOCK_AUTH_STACK_MANAGER_H_
