// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_DISK_FETCHER_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_DISK_FETCHER_H_

#include <memory>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

namespace diagnostics {
class StorageDeviceManager;

// The DiskFetcher class is responsible for gathering disk info reported by
// cros_healthd.
class DiskFetcher final {
 public:
  DiskFetcher();
  DiskFetcher(const DiskFetcher&) = delete;
  DiskFetcher& operator=(const DiskFetcher&) = delete;
  ~DiskFetcher();

  // Returns a structure with either the device's non-removable block device
  // info or the error that occurred fetching the information.
  ash::cros_healthd::mojom::NonRemovableBlockDeviceResultPtr
  FetchNonRemovableBlockDevicesInfo();

 private:
  std::unique_ptr<StorageDeviceManager> manager_;

  ash::cros_healthd::mojom::ProbeErrorPtr InitManager();
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_DISK_FETCHER_H_
