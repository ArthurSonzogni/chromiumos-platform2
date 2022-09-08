// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_UFS_DEVICE_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_UFS_DEVICE_ADAPTER_H_

#include <string>

#include <base/files/file_path.h>

#include "diagnostics/common/statusor.h"
#include "diagnostics/cros_healthd/fetchers/storage/storage_device_adapter.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

// UFS-specific data retrieval module.
class UfsDeviceAdapter : public StorageDeviceAdapter {
 public:
  explicit UfsDeviceAdapter(const base::FilePath& dev_sys_path);
  UfsDeviceAdapter(const UfsDeviceAdapter&) = delete;
  UfsDeviceAdapter(UfsDeviceAdapter&&) = delete;
  UfsDeviceAdapter& operator=(const UfsDeviceAdapter&) = delete;
  UfsDeviceAdapter& operator=(UfsDeviceAdapter&&) = delete;
  ~UfsDeviceAdapter() override = default;

  // StorageDeviceAdapter overrides.
  std::string GetDeviceName() const override;
  StatusOr<chromeos::cros_healthd::mojom::BlockDeviceVendor> GetVendorId()
      const override;
  StatusOr<chromeos::cros_healthd::mojom::BlockDeviceProduct> GetProductId()
      const override;
  StatusOr<chromeos::cros_healthd::mojom::BlockDeviceRevision> GetRevision()
      const override;
  StatusOr<std::string> GetModel() const override;
  StatusOr<chromeos::cros_healthd::mojom::BlockDeviceFirmware>
  GetFirmwareVersion() const override;

 private:
  const base::FilePath dev_sys_path_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_UFS_DEVICE_ADAPTER_H_
