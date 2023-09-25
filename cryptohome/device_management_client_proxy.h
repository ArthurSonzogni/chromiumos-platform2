// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// DeviceManagementClientProxy - proxy class for communicating with
// device_management service.

#ifndef CRYPTOHOME_DEVICE_MANAGEMENT_CLIENT_PROXY_H_
#define CRYPTOHOME_DEVICE_MANAGEMENT_CLIENT_PROXY_H_

#include <cryptohome/firmware_management_parameters_interface.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <device_management/proto_bindings/device_management_interface.pb.h>
#include <device_management-client/device_management/dbus-proxies.h>

#include <memory>

#include <base/time/time.h>
#include <base/memory/ref_counted.h>
#include <dbus/bus.h>

namespace cryptohome {
class DeviceManagementClientProxy {
 public:
  virtual ~DeviceManagementClientProxy() = default;
  DeviceManagementClientProxy() = default;
  explicit DeviceManagementClientProxy(scoped_refptr<dbus::Bus> bus);
  virtual bool IsEnterpriseOwned();

 private:
  // Proxy object to access device_management service.
  std::unique_ptr<org::chromium::DeviceManagementProxy>
      device_management_proxy_;
  const int64_t kDefaultTimeout = base::Minutes(5).InMilliseconds();
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_DEVICE_MANAGEMENT_CLIENT_PROXY_H_
