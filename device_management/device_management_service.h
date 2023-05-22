// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_MANAGEMENT_DEVICE_MANAGEMENT_SERVICE_H_
#define DEVICE_MANAGEMENT_DEVICE_MANAGEMENT_SERVICE_H_

#include "device_management/fwmp/firmware_management_parameters.h"
#include "device_management/proto_bindings/device_management_interface.pb.h"

#include <memory>

#include <libhwsec/factory/factory.h>
#include <libhwsec/frontend/cryptohome/frontend.h>

namespace device_management {
class DeviceManagementService {
 public:
  DeviceManagementService();
  ~DeviceManagementService();

  void Initialize(const hwsec::CryptohomeFrontend& hwsec_);

  // ========= Firmware Management Parameters Related Public Methods =========

  // Retrieve the firmware management parameters. Returns
  // DEVICE_MANAGEMENT_ERROR_NOT_SET if successful, and in that case, |fwmp|
  // will be filled with the firmware management parameters. Otherwise, an error
  // code is returned and |fwmp|'s content is undefined.
  device_management::DeviceManagementErrorCode GetFirmwareManagementParameters(
      device_management::FirmwareManagementParameters* fwmp);

  // Set the firmware management parameters to the value given in |fwmp|.
  // Returns DEVICE_MANAGEMENT_ERROR_NOT_SET if the operation is successful, and
  // other error code if it failed.
  device_management::DeviceManagementErrorCode SetFirmwareManagementParameters(
      const device_management::FirmwareManagementParameters& fwmp);

  // Remove the firmware management parameters, that is, undefine its NVRAM
  // space (if defined). Return true if and only if the firmware management
  // parameters are gone
  bool RemoveFirmwareManagementParameters();

 private:
  // The Firmware Management Parameters object that is used by this
  // class, but can be overridden for testing.
  std::unique_ptr<fwmp::FirmwareManagementParameters>
      firmware_management_parameters_;
};
}  // namespace device_management

#endif  // DEVICE_MANAGEMENT_DEVICE_MANAGEMENT_SERVICE_H_
