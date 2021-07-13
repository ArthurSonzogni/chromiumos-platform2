// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_MOCK_CRYPTOHOME_CLIENT_H_
#define RMAD_SYSTEM_MOCK_CRYPTOHOME_CLIENT_H_

#include "rmad/system/cryptohome_client.h"

#include <cstdint>

#include <gmock/gmock.h>

namespace rmad {

class MockCryptohomeClient : public CryptohomeClient {
 public:
  MockCryptohomeClient() = default;
  MockCryptohomeClient(const MockCryptohomeClient&) = delete;
  MockCryptohomeClient& operator=(const MockCryptohomeClient&) = delete;
  ~MockCryptohomeClient() override = default;

  MOCK_METHOD(bool, HasFwmp, (), (override));
  MOCK_METHOD(bool, IsEnrolled, (), (override));
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_MOCK_CRYPTOHOME_CLIENT_H_
