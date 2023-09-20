// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/udev/udev_device.h"

#include <blkid/blkid.h>

#include <memory>
#include <string>
#include <utility>

#include <brillo/udev/udev_device.h>
#include <brillo/udev/utils.h>

namespace rmad {

UdevDeviceImpl::UdevDeviceImpl(std::unique_ptr<brillo::UdevDevice> dev)
    : dev_(std::move(dev)), blkid_cache_(nullptr) {}

UdevDeviceImpl::~UdevDeviceImpl() {
  // Deallocate the blkid cache.
  if (blkid_cache_) {
    blkid_put_cache(blkid_cache_);
  }
}

bool UdevDeviceImpl::IsRemovable() const {
  return brillo::IsRemovable(*dev_);
}

std::string UdevDeviceImpl::GetSysPath() const {
  return std::string(dev_->GetSysPath());
}

std::string UdevDeviceImpl::GetDeviceNode() const {
  return std::string(dev_->GetDeviceNode());
}

std::string UdevDeviceImpl::GetFileSystemType() {
  const char* device_file = dev_->GetDeviceNode();
  std::string ret;
  if (blkid_cache_ || blkid_get_cache(&blkid_cache_, "/dev/null") == 0) {
    blkid_dev dev = blkid_get_dev(blkid_cache_, device_file, BLKID_DEV_NORMAL);
    if (dev) {
      const char* filesystem_type =
          blkid_get_tag_value(blkid_cache_, "TYPE", device_file);
      if (filesystem_type) {
        ret = filesystem_type;
      }
    }
  }
  return ret;
}

}  // namespace rmad
