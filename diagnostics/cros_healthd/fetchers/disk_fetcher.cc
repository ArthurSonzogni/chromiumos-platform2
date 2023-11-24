// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/disk_fetcher.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/notreached.h>
#include <base/types/expected.h>
#include <brillo/udev/udev.h>

#include "diagnostics/base/file_utils.h"
#include "diagnostics/cros_healthd/fetchers/storage/device_lister.h"
#include "diagnostics/cros_healthd/fetchers/storage/device_manager.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

}  // namespace

mojom::ProbeErrorPtr DiskFetcher::InitManager() {
  auto udev = brillo::Udev::Create();
  if (!udev)
    return mojom::ProbeError::New(mojom::ErrorType::kSystemUtilityError,
                                  "Unable to create udev interface");

  manager_ = std::make_unique<StorageDeviceManager>(
      std::make_unique<StorageDeviceLister>(), std::move(udev),
      std::make_unique<Platform>());
  return nullptr;
}

mojom::NonRemovableBlockDeviceResultPtr
DiskFetcher::FetchNonRemovableBlockDevicesInfo() {
  if (!manager_) {
    if (auto error = InitManager(); !error.is_null()) {
      return mojom::NonRemovableBlockDeviceResult::NewError(std::move(error));
    }
  }

  if (auto devices_result = manager_->FetchDevicesInfo(GetRootDir());
      devices_result.has_value()) {
    return mojom::NonRemovableBlockDeviceResult::NewBlockDeviceInfo(
        std::move(devices_result.value()));
  } else {
    return mojom::NonRemovableBlockDeviceResult::NewError(
        std::move(devices_result.error()));
  }
}

}  // namespace diagnostics
