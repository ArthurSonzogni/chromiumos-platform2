// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_DEVICE_MANAGEMENT_CLIENT_IMPL_H_
#define RMAD_SYSTEM_DEVICE_MANAGEMENT_CLIENT_IMPL_H_

#include <cstdint>
#include <memory>

#include <device_management-client/device_management/dbus-proxies.h>

#include "rmad/system/device_management_client.h"

namespace rmad {

class DeviceManagementClientImpl : public DeviceManagementClient {
 public:
  DeviceManagementClientImpl();
  explicit DeviceManagementClientImpl(
      std::unique_ptr<org::chromium::DeviceManagementProxyInterface>
          device_management_proxy);
  DeviceManagementClientImpl(const DeviceManagementClientImpl&) = delete;
  DeviceManagementClientImpl& operator=(const DeviceManagementClientImpl&) =
      delete;

  ~DeviceManagementClientImpl() override;

  bool IsCcdBlocked() override;

 private:
  bool GetFwmp(uint32_t* flags);

  std::unique_ptr<org::chromium::DeviceManagementProxyInterface>
      device_management_proxy_;
};

}  // namespace rmad

#endif  // RMAD_SYSTEM_DEVICE_MANAGEMENT_CLIENT_IMPL_H_
