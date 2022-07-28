// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_EXECUTOR_UDEV_UDEV_DEVICE_H_
#define RMAD_EXECUTOR_UDEV_UDEV_DEVICE_H_

#include <blkid/blkid.h>

#include <memory>
#include <string>

namespace brillo {
class UdevDevice;
}  // namespace brillo

namespace rmad {

class UdevDevice {
 public:
  explicit UdevDevice(std::unique_ptr<brillo::UdevDevice> dev);
  virtual ~UdevDevice();

  bool IsRemovable() const;
  std::string GetSysPath() const;
  std::string GetDeviceNode() const;
  // Not a const method because it updates |blkid_cache_|.
  std::string GetFileSystemType();

 private:
  std::unique_ptr<brillo::UdevDevice> dev_;
  blkid_cache blkid_cache_;
};

}  // namespace rmad

#endif  // RMAD_EXECUTOR_UDEV_UDEV_DEVICE_H_
