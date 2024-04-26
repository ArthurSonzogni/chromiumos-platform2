// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_SYSTEM_MOCK_USER_DATA_AUTH_CLIENT_H_
#define POWER_MANAGER_POWERD_SYSTEM_MOCK_USER_DATA_AUTH_CLIENT_H_

#include "power_manager/powerd/system/user_data_auth_client.h"

#include <gmock/gmock.h>

namespace power_manager::system {

// Mock implementation of UserDataAuthClient used by tests.
class MockUserDataAuthClient : public UserDataAuthClient {
 public:
  ~MockUserDataAuthClient() = default;

  MOCK_METHOD(bool, EvictDeviceKey, (int), (override));
};
}  // namespace power_manager::system

#endif  // POWER_MANAGER_POWERD_SYSTEM_MOCK_USER_DATA_AUTH_CLIENT_H_
