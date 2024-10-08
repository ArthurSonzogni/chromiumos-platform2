// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mock_device_management_client_proxy.h"

#include "cryptohome/device_management_client_proxy.h"

namespace cryptohome {

MockDeviceManagementClientProxy::MockDeviceManagementClientProxy()
    : DeviceManagementClientProxy() {}
MockDeviceManagementClientProxy::~MockDeviceManagementClientProxy() {}

}  // namespace cryptohome
