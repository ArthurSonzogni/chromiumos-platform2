// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_HWIS_FLEX_HWIS_CHECK_H_
#define FLEX_HWIS_FLEX_HWIS_CHECK_H_

#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <policy/device_policy.h>
#include <policy/libpolicy.h>

namespace flex_hwis {
class PermissionInfo {
 public:
  // If the device is managed, the value of this field is true.
  bool managed = false;
  // If the device policy is successfully loaded to this device,
  // the value of this field is true.
  bool loaded = false;
  // The value of this field is true if all required device management
  // policies are enabled in managed devices, or consent has been granted
  // through OOBE in unmanaged devices.
  bool permission = false;
};

// This class is for processing management checking items utilized in HWIS.
class FlexHwisCheck {
 public:
  explicit FlexHwisCheck(const base::FilePath& base_path,
                         policy::PolicyProvider& provider);
  // Verify if the device is authorized to send hardware data to the server.
  // In the managed case, management policies should be checked. In the
  // current version, there is no fine-grained control. If one of the policies
  // is disabled, hardware data will not be uploaded. In the unmanaged case,
  // hardware_data_usage_enabled should be checked.
  PermissionInfo CheckPermission();

  // Check if the HWIS has run in the last 24 hours.
  bool HasRunRecently();

  // If the hardware data is successfully sent to the server, the new
  // timestamp will be stored.
  void RecordSendTime();

  // Retrieve a device name.
  std::optional<std::string> GetDeviceName() const;

  // Delete the file that stores the device name.
  void DeleteDeviceName();

  // Write a device name to the client side.
  void SetDeviceName(const std::string_view name);

 private:
  // Extracts the HWIS info from the file at the file_path.
  std::optional<std::string> ReadHwisFile(
      const base::FilePath& file_path) const;

  // Writes the HWIS information, such as device name or timestamp, to file_path
  // and adds a newline.
  bool WriteHwisFile(const base::FilePath& file_path,
                     const std::string_view content);

  // Returns the file path that records the device name created by the
  // HWIS server.
  base::FilePath DeviceNamePath() const;

  base::FilePath base_path_;

  // The device policy provider, used to get device policy data.
  policy::PolicyProvider& policy_provider_;
};

}  // namespace flex_hwis

#endif  // FLEX_HWIS_FLEX_HWIS_CHECK_H_
