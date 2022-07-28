// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_EXECUTOR_UDEV_UDEV_UTILS_H_
#define RMAD_EXECUTOR_UDEV_UDEV_UTILS_H_

#include <memory>
#include <string>
#include <vector>

namespace brillo {
class Udev;
}  // namespace brillo

namespace rmad {

class UdevDevice;

class UdevUtils {
 public:
  explicit UdevUtils(std::unique_ptr<brillo::Udev> udev);
  virtual ~UdevUtils();

  std::vector<std::unique_ptr<UdevDevice>> EnumerateBlockDevices();
  bool GetBlockDeviceFromDevicePath(const std::string& device_path,
                                    std::unique_ptr<UdevDevice>* dev);

 private:
  std::unique_ptr<brillo::Udev> udev_;
};

class UdevUtilsImpl : public UdevUtils {
 public:
  UdevUtilsImpl();
  ~UdevUtilsImpl() override;
};

}  // namespace rmad

#endif  // RMAD_EXECUTOR_UDEV_UDEV_UTILS_H_
