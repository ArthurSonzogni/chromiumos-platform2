// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_MOCK_CRYPTOHOME_CLIENT_H_
#define POWER_MANAGER_POWERD_SYSTEM_MOCK_CRYPTOHOME_CLIENT_H_

#include "power_manager/powerd/system/cryptohome_client.h"

#include <gmock/gmock.h>

namespace power_manager::system {

// Mock implementation of CryptohomeClient used by tests.
class MockCryptohomeClient : public CryptohomeClient {
 public:
  ~MockCryptohomeClient() = default;

  MOCK_METHOD(void, EvictDeviceKey, (int), (override));
};
}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_MOCK_CRYPTOHOME_CLIENT_H_
