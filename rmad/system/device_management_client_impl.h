// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_SYSTEM_DEVICE_MANAGEMENT_CLIENT_IMPL_H_
#define RMAD_SYSTEM_DEVICE_MANAGEMENT_CLIENT_IMPL_H_

#include "rmad/system/device_management_client.h"

#include <cstdint>
#include <memory>

#include <base/memory/scoped_refptr.h>
#include <cryptohome/proto_bindings/rpc.pb.h>
#include <dbus/bus.h>
#include <device_management/proto_bindings/device_management_interface.pb.h>

namespace org {
namespace chromium {
class DeviceManagementProxyInterface;
}  // namespace chromium
}  // namespace org

namespace rmad {

class DeviceManagementClientImpl : public DeviceManagementClient {
 public:
  explicit DeviceManagementClientImpl(const scoped_refptr<dbus::Bus>& bus);
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
