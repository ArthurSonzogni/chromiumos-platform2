// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_MOCK_DEVICE_MANAGEMENT_CLIENT_H_
#define RMAD_SYSTEM_MOCK_DEVICE_MANAGEMENT_CLIENT_H_

#include "rmad/system/device_management_client.h"

#include <gmock/gmock.h>

namespace rmad {

class MockDeviceManagementClient : public DeviceManagementClient {
 public:
  MockDeviceManagementClient() = default;
  MockDeviceManagementClient(const MockDeviceManagementClient&) = delete;
  MockDeviceManagementClient& operator=(const MockDeviceManagementClient&) =
      delete;
  ~MockDeviceManagementClient() override = default;

  MOCK_METHOD(bool, IsCcdBlocked, (), (override));
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_MOCK_DEVICE_MANAGEMENT_CLIENT_H_
