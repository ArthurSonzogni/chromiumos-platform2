// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_DISK_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_DISK_FETCHER_H_

#include <memory>

#include <base/files/file_path.h>

#include "diagnostics/cros_healthd/fetchers/storage/device_manager.h"
#include "diagnostics/cros_healthd/system/context.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

// The DiskFetcher class is responsible for gathering disk info reported by
// cros_healthd.
class DiskFetcher {
 public:
  explicit DiskFetcher(Context* context);
  ~DiskFetcher() = default;
  DiskFetcher(const DiskFetcher&) = delete;
  DiskFetcher& operator=(const DiskFetcher&) = delete;

  // Returns a structure with either the device's non-removable block device
  // info or the error that occurred fetching the information.
  chromeos::cros_healthd::mojom::NonRemovableBlockDeviceResultPtr
  FetchNonRemovableBlockDevicesInfo();

 private:
  std::unique_ptr<StorageDeviceManager> manager_;

  // Unowned pointer that outlives this DiskFetcher instance.
  Context* const context_ = nullptr;

  Status InitManager();
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_DISK_FETCHER_H_
