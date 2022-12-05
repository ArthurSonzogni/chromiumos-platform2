// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "brillo/blkdev_utils/get_backing_block_device.h"

#include <memory>
#include <string>
#include <utility>

#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/udev/udev.h>
#include <brillo/udev/udev_device.h>
#include <rootdev/rootdev.h>

namespace brillo {

namespace {

base::FilePath GetDeviceNode(Udev* udev, dev_t devnum) {
  auto dev = udev->CreateDeviceFromDeviceNumber('b', devnum);
  if (!dev) {
    LOG(WARNING) << "Could not get udev entry for device with MAJOR: "
                 << major(devnum) << " MINOR: " << minor(devnum);
    return base::FilePath();
  }

  return base::FilePath(dev->GetDeviceNode());
}

}  // namespace

base::FilePath GetBackingPhysicalDeviceForBlock(
    const base::FilePath& dev_node, const std::string& search_path) {
  char dst[PATH_MAX];
  dev_t backing_dev;
  rootdev_get_device_slave(dst, PATH_MAX, &backing_dev,
                           dev_node.BaseName().value().c_str(),
                           search_path.empty() ? nullptr : search_path.c_str());
  rootdev_strip_partition(dst, PATH_MAX);
  return base::FilePath("/dev/").Append(dst);
}

base::FilePath GetBackingPhysicalDeviceForBlock(dev_t devnum,
                                                const std::string& search_path,
                                                std::unique_ptr<Udev> udev) {
  base::FilePath dev_node = GetDeviceNode(udev.get(), devnum);
  if (dev_node.empty()) {
    LOG(WARNING) << "Could not find device node for MAJOR: " << major(devnum)
                 << " MINOR: " << minor(devnum);
    return base::FilePath();
  }

  return GetBackingPhysicalDeviceForBlock(dev_node, search_path);
}

base::FilePath GetBackingPhysicalDeviceForFile(const base::FilePath& path) {
  struct stat stat_buf;
  if (stat(path.value().c_str(), &stat_buf) < 0) {
    PLOG(WARNING) << "Could not stat: " << path.value();
    return base::FilePath();
  }

  return GetBackingPhysicalDeviceForBlock(stat_buf.st_dev);
}

}  // namespace brillo
