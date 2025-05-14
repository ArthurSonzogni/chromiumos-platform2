/*
 * Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/utils/camera_hal_enumerator.h"

#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>

#include "cros-camera/common.h"
#include "cros-camera/device_config.h"

namespace cros {

std::vector<base::FilePath> GetCameraHalPaths() {
  const base::FilePath kCameraHalDirs[] = {
      base::FilePath("/usr/lib/camera_hal"),
      base::FilePath("/usr/lib64/camera_hal")};

  std::vector<base::FilePath> camera_hal_paths;
  std::optional<DeviceConfig> device_config = DeviceConfig::Create();
  bool has_mipi = device_config && device_config->HasMipiCamera();

  // Reven is not a single SKU but can be an arbitrary device that may have
  // a MIPI camera, which will not be reflected by the device_config because
  // we do not know what cameras exist until loading them. Always load the
  // libcamera DLL so we can load any MIPI cameras.
  bool is_reven = device_config && device_config->GetModelName() == "reven";

  for (base::FilePath dir : kCameraHalDirs) {
    base::FileEnumerator dlls(dir, false, base::FileEnumerator::FILES, "*.so");
    for (base::FilePath dll = dlls.Next(); !dll.empty(); dll = dlls.Next()) {
      auto filename = dll.BaseName().value();
      if (filename != "usb.so" && filename != "fake.so" &&
          filename != "ip.so" && filename != "cavern.so" && !has_mipi &&
          !is_reven) {
        LOGF(INFO) << "No MIPI camera so skip camera hal " << dll.value();
        continue;
      }
      camera_hal_paths.push_back(dll);
    }
  }

  return camera_hal_paths;
}

}  // namespace cros
