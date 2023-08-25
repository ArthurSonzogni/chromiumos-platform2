// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FRONTEND_PINWEAVER_MANAGER_MOCK_FRONTEND_H_
#define LIBHWSEC_FRONTEND_PINWEAVER_MANAGER_MOCK_FRONTEND_H_

#include <cstdint>
#include <vector>

#include <absl/container/flat_hash_set.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libhwsec/frontend/mock_frontend.h"
#include "libhwsec/frontend/pinweaver_manager/frontend.h"

namespace hwsec {

class MockPinWeaverManagerFrontend : public MockFrontend,
                                     public PinWeaverManagerFrontend {
 public:
  MockPinWeaverManagerFrontend() = default;
  ~MockPinWeaverManagerFrontend() override = default;

  MOCK_METHOD(StatusOr<bool>, IsEnabled, (), (const override));
  MOCK_METHOD(StatusOr<uint8_t>, GetVersion, (), (const override));
  MOCK_METHOD(StatusOr<uint64_t>,
              InsertCredential,
              (const std::vector<OperationPolicySetting>& policies,
               const brillo::SecureBlob& le_secret,
               const brillo::SecureBlob& he_secret,
               const brillo::SecureBlob& reset_secret,
               const DelaySchedule& delay_schedule,
               std::optional<uint32_t> expiration_delay),
              (const override));
  MOCK_METHOD(StatusOr<CheckCredentialReply>,
              CheckCredential,
              (const uint64_t label, const brillo::SecureBlob& le_secret),
              (const override));
  MOCK_METHOD(Status,
              RemoveCredential,
              (const uint64_t label),
              (const override));
  MOCK_METHOD(Status,
              ResetCredential,
              (const uint64_t label,
               const brillo::SecureBlob& reset_secret,
               ResetType reset_type),
              (const override));
  MOCK_METHOD(StatusOr<uint32_t>,
              GetWrongAuthAttempts,
              (const uint64_t label),
              (const override));
  MOCK_METHOD(StatusOr<DelaySchedule>,
              GetDelaySchedule,
              (const uint64_t label),
              (const override));
  MOCK_METHOD(StatusOr<uint32_t>,
              GetDelayInSeconds,
              (const uint64_t label),
              (const override));
  MOCK_METHOD(StatusOr<std::optional<uint32_t>>,
              GetExpirationInSeconds,
              (const uint64_t label),
              (const override));
  MOCK_METHOD(StatusOr<PinWeaverEccPoint>,
              GeneratePk,
              (uint8_t auth_channel,
               const PinWeaverEccPoint& client_public_key),
              (const override));
  MOCK_METHOD(StatusOr<uint64_t>,
              InsertRateLimiter,
              (uint8_t auth_channel,
               const std::vector<OperationPolicySetting>& policies,
               const brillo::SecureBlob& reset_secret,
               const DelaySchedule& delay_schedule,
               std::optional<uint32_t> expiration_delay),
              (const override));
  MOCK_METHOD(StatusOr<StartBiometricsAuthReply>,
              StartBiometricsAuth,
              (uint8_t auth_channel,
               const uint64_t label,
               const brillo::Blob& client_nonce),
              (const override));
  MOCK_METHOD(Status, BlockGeneratePk, (), (const override));
};

}  // namespace hwsec

#endif  // LIBHWSEC_FRONTEND_PINWEAVER_MANAGER_MOCK_FRONTEND_H_
