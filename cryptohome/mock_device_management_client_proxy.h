// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_DEVICE_MANAGEMENT_CLIENT_PROXY_H_
#define CRYPTOHOME_MOCK_DEVICE_MANAGEMENT_CLIENT_PROXY_H_

#include "cryptohome/device_management_client_proxy.h"

#include <gmock/gmock.h>

namespace cryptohome {
class MockDeviceManagementClientProxy : public DeviceManagementClientProxy {
 public:
  MockDeviceManagementClientProxy();
  virtual ~MockDeviceManagementClientProxy();

  MOCK_METHOD(bool, IsEnterpriseOwned, (), (override));
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_DEVICE_MANAGEMENT_CLIENT_PROXY_H_
