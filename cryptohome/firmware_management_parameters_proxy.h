// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// FirmwareManagementParametersProxy - forwards fwmp related requests to
// device_management service

#ifndef CRYPTOHOME_FIRMWARE_MANAGEMENT_PARAMETERS_PROXY_H_
#define CRYPTOHOME_FIRMWARE_MANAGEMENT_PARAMETERS_PROXY_H_

#include "cryptohome/firmware_management_parameters_interface.h"

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <device_management/proto_bindings/device_management_interface.pb.h>
#include <device_management-client/device_management/dbus-proxies.h>

#include <memory>

#include <base/time/time.h>

namespace cryptohome {
class FirmwareManagementParametersProxy
    : public FirmwareManagementParametersInterface {
 public:
  FirmwareManagementParametersProxy() = default;
  FirmwareManagementParametersProxy(const FirmwareManagementParametersProxy&) =
      delete;
  FirmwareManagementParametersProxy& operator=(
      const FirmwareManagementParametersProxy&) = delete;

  ~FirmwareManagementParametersProxy() = default;

  bool GetFWMP(user_data_auth::FirmwareManagementParameters* fwmp) override;

  bool SetFWMP(
      const user_data_auth::FirmwareManagementParameters& fwmp) override;

  bool Destroy() override;

  void SetDeviceManagementProxy(
      std::unique_ptr<org::chromium::DeviceManagementProxy> proxy) override;

 private:
  // Proxy object to access device_management service.
  std::unique_ptr<org::chromium::DeviceManagementProxy>
      device_management_proxy_;
  const int64_t kDefaultTimeout = base::Minutes(5).InMilliseconds();
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_FIRMWARE_MANAGEMENT_PARAMETERS_PROXY_H_
