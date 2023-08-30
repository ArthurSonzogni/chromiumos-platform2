// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_MANAGEMENT_DEVICE_MANAGEMENT_SERVICE_H_
#define DEVICE_MANAGEMENT_DEVICE_MANAGEMENT_SERVICE_H_

#include "device_management/fwmp/firmware_management_parameters.h"
#include "device_management/install_attributes/install_attributes.h"
#include "device_management/install_attributes/platform.h"
#include "device_management/proto_bindings/device_management_interface.pb.h"

#include <memory>
#include <string>
#include <vector>

#include <libhwsec/factory/factory.h>
#include <libhwsec/frontend/cryptohome/frontend.h>
#include <libhwsec/status.h>

namespace device_management {
class DeviceManagementService {
 public:
  DeviceManagementService();
  ~DeviceManagementService();

  void Initialize(const hwsec::CryptohomeFrontend& hwsec_, Platform& platform);

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

  // =============== Install Attributes Related Public Methods ===============

  // Retrieve the key value pair in install attributes with the key of |name|,
  // and return its value in |data_out|. Returns true if and only if the key
  // value pair is successfully retrieved. If false is returned, then
  // |data_out|'s content is undefined.
  bool InstallAttributesGet(const std::string& name,
                            std::vector<uint8_t>* data_out);

  // Insert the key value pair (name, data) into install attributes. Return true
  // if and only if the key value pair is successfully inserted.
  bool InstallAttributesSet(const std::string& name,
                            const std::vector<uint8_t>& data);

  // Finalize the install attributes. Return true if and only if the install
  // attributes is finalized.
  bool InstallAttributesFinalize();

  // Get the number of key value pair stored in install attributes.
  int InstallAttributesCount();

  // Return true if and only if the attribute storage is securely stored, that
  // is, if the system TPM/Lockbox is being used.
  bool InstallAttributesIsSecure();

  // Return the current status of the install attributes.
  InstallAttributes::Status InstallAttributesGetStatus();

  // Convert the InstallAttributes::Status enum to
  // user_data_auth::InstallAttributesState protobuf enum.
  static device_management::InstallAttributesState
  InstallAttributesStatusToProtoEnum(InstallAttributes::Status status);

  // =============== Install Attributes Related Utilities ===============

  // Set whether this device is enterprise owned. Calling this method will have
  // effect on all currently mounted mounts. This can only be called on
  // mount_thread_.
  void SetEnterpriseOwned(bool enterprise_owned);

  // Detect whether this device is enterprise owned, and call
  // SetEnterpriseOwned(). This can only be called on origin thread.
  void DetectEnterpriseOwnership();

  // Call this method to initialize the install attributes functionality. This
  // can only be called on origin thread.
  void InitializeInstallAttributesCallback(hwsec::Status status);

  // Return true if this device is enterprise owned.
  bool IsEnterpriseOwned() { return enterprise_owned_; }

 private:
  // The Firmware Management Parameters object that is used by this
  // class, but can be overridden for testing.
  std::unique_ptr<fwmp::FirmwareManagementParameters>
      firmware_management_parameters_;
  // The install attributes object used by this class, usually set to
  // |default_install_attrs_|, but can be overridden for testing.
  std::unique_ptr<InstallAttributes> install_attrs_;
  // Whether this device is an enterprise owned device.
  bool enterprise_owned_;
};
}  // namespace device_management

#endif  // DEVICE_MANAGEMENT_DEVICE_MANAGEMENT_SERVICE_H_
