// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_DEVICE_MANAGEMENT_CLIENT_PROXY_H_
#define CRYPTOHOME_MOCK_DEVICE_MANAGEMENT_CLIENT_PROXY_H_

#include <gmock/gmock.h>

#include "cryptohome/device_management_client_proxy.h"

namespace cryptohome {
class MockDeviceManagementClientProxy : public DeviceManagementClientProxy {
 public:
  MockDeviceManagementClientProxy();
  virtual ~MockDeviceManagementClientProxy();

  MOCK_METHOD(bool, IsEnterpriseOwned, (), (override));
  MOCK_METHOD(bool, IsInstallAttributesFirstInstall, (), (override));
  MOCK_METHOD(bool, InstallAttributesFinalize, (), (override));
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_DEVICE_MANAGEMENT_CLIENT_PROXY_H_
