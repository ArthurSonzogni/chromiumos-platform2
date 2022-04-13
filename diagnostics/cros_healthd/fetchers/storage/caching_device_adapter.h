// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_CACHING_DEVICE_ADAPTER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_CACHING_DEVICE_ADAPTER_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "diagnostics/common/status_macros.h"
#include "diagnostics/common/statusor.h"
#include "diagnostics/cros_healthd/fetchers/storage/storage_device_adapter.h"

namespace diagnostics {

// A caching decorator for the device-specific adapters. Its purpose is to
// eliminate repeating calls into the kernel and hardware.
class CachingDeviceAdapter final : public StorageDeviceAdapter {
 public:
  explicit CachingDeviceAdapter(std::unique_ptr<StorageDeviceAdapter> adapter);
  CachingDeviceAdapter(const CachingDeviceAdapter&) = delete;
  CachingDeviceAdapter(CachingDeviceAdapter&&) = delete;
  CachingDeviceAdapter& operator=(const CachingDeviceAdapter&) = delete;
  CachingDeviceAdapter& operator=(CachingDeviceAdapter&&) = delete;
  ~CachingDeviceAdapter() override = default;

  std::string GetDeviceName() const override;
  StatusOr<ash::cros_healthd::mojom::BlockDeviceVendor> GetVendorId()
      const override;
  StatusOr<ash::cros_healthd::mojom::BlockDeviceProduct> GetProductId()
      const override;
  StatusOr<ash::cros_healthd::mojom::BlockDeviceRevision> GetRevision()
      const override;
  StatusOr<std::string> GetModel() const override;
  StatusOr<ash::cros_healthd::mojom::BlockDeviceFirmware> GetFirmwareVersion()
      const override;

 private:
  const std::unique_ptr<const StorageDeviceAdapter> adapter_;

  // The fields have to be mutable because of the const interface.
  mutable std::optional<std::string> device_name_;
  mutable std::optional<ash::cros_healthd::mojom::BlockDeviceVendor> vendor_id_;
  mutable std::optional<ash::cros_healthd::mojom::BlockDeviceProduct>
      product_id_;
  mutable std::optional<ash::cros_healthd::mojom::BlockDeviceRevision>
      revision_;
  mutable std::optional<std::string> model_;
  mutable std::optional<ash::cros_healthd::mojom::BlockDeviceFirmware>
      firmware_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_STORAGE_CACHING_DEVICE_ADAPTER_H_
