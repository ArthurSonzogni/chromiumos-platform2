// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_MOCK_TPM_MANAGER_CLIENT_H_
#define RMAD_SYSTEM_MOCK_TPM_MANAGER_CLIENT_H_

#include <gmock/gmock.h>

#include "rmad/system/tpm_manager_client.h"

namespace rmad {

class MockTpmManagerClient : public TpmManagerClient {
 public:
  MockTpmManagerClient() = default;
  MockTpmManagerClient(const MockTpmManagerClient&) = delete;
  MockTpmManagerClient& operator=(const MockTpmManagerClient&) = delete;
  ~MockTpmManagerClient() override = default;

  MOCK_METHOD(bool,
              GetRoVerificationStatus,
              (RoVerificationStatus*),
              (override));
  MOCK_METHOD(bool, GetGscDevice, (GscDevice*), (override));
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_MOCK_TPM_MANAGER_CLIENT_H_
