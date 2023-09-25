// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// FirmwareManagementParameters interface - interface for storing firmware
// management parameters to TPM

#ifndef CRYPTOHOME_FIRMWARE_MANAGEMENT_PARAMETERS_INTERFACE_H_
#define CRYPTOHOME_FIRMWARE_MANAGEMENT_PARAMETERS_INTERFACE_H_

#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <device_management/proto_bindings/device_management_interface.pb.h>
#include <device_management-client/device_management/dbus-proxies.h>

#include <memory>
#include <string>

#include <brillo/secure_blob.h>

namespace cryptohome {
class FirmwareManagementParametersInterface {
 public:
  virtual ~FirmwareManagementParametersInterface() = default;

  // Fetches firmware management parameters.
  // Returns
  // - true if successful.
  // - false if not.
  virtual bool GetFWMP(user_data_auth::FirmwareManagementParameters* fwmp) = 0;

  // Sets the firmware management parameters.
  // Returns
  // - true if successful.
  // - false if not.
  virtual bool SetFWMP(
      const user_data_auth::FirmwareManagementParameters& fwmp) = 0;

  // Destroys all backend state for this firmware management parameters.
  //
  // This call deletes the NVRAM space if defined.
  //
  // Returns
  // - false if TPM Owner authorization is missing or the space cannot be
  //   destroyed.
  // - true if the space is already undefined or has been destroyed.
  virtual bool Destroy() = 0;

  // Sets the device_management proxy for forwarding requests to
  // device_management service. This is a no-op for legacy install_attributes.
  virtual void SetDeviceManagementProxy(
      std::unique_ptr<org::chromium::DeviceManagementProxy> proxy) = 0;
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_FIRMWARE_MANAGEMENT_PARAMETERS_INTERFACE_H_
