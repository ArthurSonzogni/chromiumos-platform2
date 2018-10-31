// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/boot_device.h"

#include <linux/limits.h>

#include <base/files/file_util.h>
#include <base/strings/string_util.h>
#include <rootdev/rootdev.h>

namespace dlcservice {

bool BootDevice::IsRemovableDevice(const std::string& device) {
  std::string sysfs_block = SysfsBlockDevice(device);
  std::string removable;
  if (sysfs_block.empty() ||
      !base::ReadFileToString(base::FilePath(sysfs_block).Append("removable"),
                              &removable)) {
    return false;
  }
  base::TrimWhitespaceASCII(removable, base::TRIM_ALL, &removable);
  return removable == "1";
}

std::string BootDevice::GetBootDevice() {
  char boot_path[PATH_MAX];
  // Resolve the boot device path fully, including dereferencing through
  // dm-verity.
  int ret = rootdev(boot_path, sizeof(boot_path), true, /*full resolution*/
                    false /*do not remove partition #*/);
  if (ret < 0) {
    LOG(ERROR) << "rootdev failed to find the root device";
    return "";
  }
  LOG_IF(WARNING, ret > 0) << "rootdev found a device name with no device node";

  // This local variable is used to construct the return string and is not
  // passed around after use.
  return boot_path;
}

std::string BootDevice::SysfsBlockDevice(const std::string& device) {
  base::FilePath device_path(device);
  if (device_path.DirName().value() != "/dev") {
    return "";
  }
  return base::FilePath("/sys/block").Append(device_path.BaseName()).value();
}

}  // namespace dlcservice
