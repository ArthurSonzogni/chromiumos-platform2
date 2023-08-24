// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_PINWEAVER_MANAGER_H_
#define LIBHWSEC_BACKEND_MOCK_PINWEAVER_MANAGER_H_

#include <cstdint>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libhwsec/backend/pinweaver_manager/pinweaver_manager.h"

namespace hwsec {

class MockLECredentialManager : public LECredentialManager {
 public:
  MockLECredentialManager() = default;
  explicit MockLECredentialManager(LECredentialManager* on_call)
      : default_(on_call) {
    using testing::Invoke;
    if (!default_)
      return;
    ON_CALL(*this, InsertCredential)
        .WillByDefault(
            Invoke(default_, &LECredentialManager::InsertCredential));
    ON_CALL(*this, CheckCredential)
        .WillByDefault(Invoke(default_, &LECredentialManager::CheckCredential));
    ON_CALL(*this, RemoveCredential)
        .WillByDefault(
            Invoke(default_, &LECredentialManager::RemoveCredential));
    ON_CALL(*this, ResetCredential)
        .WillByDefault(Invoke(default_, &LECredentialManager::ResetCredential));
    ON_CALL(*this, GetWrongAuthAttempts)
        .WillByDefault(
            Invoke(default_, &LECredentialManager::GetWrongAuthAttempts));
    ON_CALL(*this, GetDelaySchedule)
        .WillByDefault(
            Invoke(default_, &LECredentialManager::GetDelaySchedule));
    ON_CALL(*this, GetDelayInSeconds)
        .WillByDefault(
            Invoke(default_, &LECredentialManager::GetDelayInSeconds));
    ON_CALL(*this, GetExpirationInSeconds)
        .WillByDefault(
            Invoke(default_, &LECredentialManager::GetExpirationInSeconds));
    ON_CALL(*this, InsertRateLimiter)
        .WillByDefault(
            Invoke(default_, &LECredentialManager::InsertRateLimiter));
    ON_CALL(*this, StartBiometricsAuth)
        .WillByDefault(
            Invoke(default_, &LECredentialManager::StartBiometricsAuth));
  }

  MOCK_METHOD(StatusOr<uint64_t>,
              InsertCredential,
              (const std::vector<hwsec::OperationPolicySetting>& policies,
               const brillo::SecureBlob& le_secret,
               const brillo::SecureBlob& he_secret,
               const brillo::SecureBlob& reset_secret,
               const DelaySchedule& delay_sched,
               std::optional<uint32_t> expiration_delay),
              (override));
  MOCK_METHOD(StatusOr<CheckCredentialReply>,
              CheckCredential,
              (const uint64_t label, const brillo::SecureBlob& le_secret),
              (override));
  MOCK_METHOD(Status, RemoveCredential, (const uint64_t label), (override));
  MOCK_METHOD(Status,
              ResetCredential,
              (const uint64_t label,
               const brillo::SecureBlob& reset_secret,
               ResetType reset_type),
              (override));
  MOCK_METHOD(StatusOr<uint32_t>,
              GetWrongAuthAttempts,
              (const uint64_t label),
              (override));
  MOCK_METHOD(StatusOr<DelaySchedule>,
              GetDelaySchedule,
              (const uint64_t label),
              (override));
  MOCK_METHOD(StatusOr<uint32_t>,
              GetDelayInSeconds,
              (const uint64_t label),
              (override));
  MOCK_METHOD(StatusOr<std::optional<uint32_t>>,
              GetExpirationInSeconds,
              (const uint64_t label),
              (override));
  MOCK_METHOD(StatusOr<uint64_t>,
              InsertRateLimiter,
              (uint8_t auth_channel,
               const std::vector<hwsec::OperationPolicySetting>& policies,
               const brillo::SecureBlob& reset_secret,
               const DelaySchedule& delay_sched,
               std::optional<uint32_t> expiration_delay),
              (override));
  MOCK_METHOD(StatusOr<StartBiometricsAuthReply>,
              StartBiometricsAuth,
              (uint8_t auth_channel,
               uint64_t label,
               const brillo::Blob& client_nonce),
              (override));

 private:
  LECredentialManager* default_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_PINWEAVER_MANAGER_H_
