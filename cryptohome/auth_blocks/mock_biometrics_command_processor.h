// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_AUTH_BLOCKS_MOCK_BIOMETRICS_COMMAND_PROCESSOR_H_
#define CRYPTOHOME_AUTH_BLOCKS_MOCK_BIOMETRICS_COMMAND_PROCESSOR_H_

#include "cryptohome/auth_blocks/biometrics_command_processor.h"

#include <optional>
#include <string>

#include <gmock/gmock.h>

namespace cryptohome {

class MockBiometricsCommandProcessor : public BiometricsCommandProcessor {
 public:
  MockBiometricsCommandProcessor() = default;
  MOCK_METHOD(
      void,
      SetEnrollScanDoneCallback,
      (base::RepeatingCallback<void(user_data_auth::AuthEnrollmentProgress)>),
      (override));
  MOCK_METHOD(bool, IsReady, (), (override));
  MOCK_METHOD(void,
              SetAuthScanDoneCallback,
              (base::RepeatingCallback<void(user_data_auth::AuthScanDone)>),
              (override));
  MOCK_METHOD(void,
              SetSessionFailedCallback,
              (base::RepeatingCallback<void()>),
              (override));
  MOCK_METHOD(void,
              GetNonce,
              (base::OnceCallback<void(std::optional<brillo::Blob>)>),
              (override));
  MOCK_METHOD(void,
              StartEnrollSession,
              (OperationInput payload, base::OnceCallback<void(bool)>),
              (override));
  MOCK_METHOD(void,
              StartAuthenticateSession,
              (ObfuscatedUsername,
               OperationInput payload,
               base::OnceCallback<void(bool)>),
              (override));
  MOCK_METHOD(void, CreateCredential, (OperationCallback), (override));
  MOCK_METHOD(void, MatchCredential, (OperationCallback), (override));
  MOCK_METHOD(void, EndEnrollSession, (), (override));
  MOCK_METHOD(void, EndAuthenticateSession, (), (override));
  MOCK_METHOD(void,
              DeleteCredential,
              (ObfuscatedUsername,
               const std::string&,
               base::OnceCallback<void(DeleteResult)>),
              (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_AUTH_BLOCKS_MOCK_BIOMETRICS_COMMAND_PROCESSOR_H_
