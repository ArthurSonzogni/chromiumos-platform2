// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libstorage/storage_container/backing_device_factory.h"

#include <memory>

#include "libstorage/platform/platform.h"
#include "libstorage/storage_container/backing_device.h"
#include "libstorage/storage_container/logical_volume_backing_device.h"
#include "libstorage/storage_container/ramdisk_device.h"

namespace libstorage {

BackingDeviceFactory::BackingDeviceFactory(Platform* platform)
    : platform_(platform) {}

std::unique_ptr<BackingDevice> BackingDeviceFactory::Generate(
    const BackingDeviceConfig& config) {
  switch (config.type) {
    case BackingDeviceType::kLoopbackDevice:
      return std::make_unique<LoopbackDevice>(config, platform_);
    case BackingDeviceType::kRamdiskDevice:
      return RamdiskDevice::Generate(config.ramdisk.backing_file_path,
                                     platform_);
    case BackingDeviceType::kLogicalVolumeBackingDevice:
      return std::make_unique<LogicalVolumeBackingDevice>(
          config, platform_->GetLogicalVolumeManager());
    default:
      return nullptr;
  }
}

}  // namespace libstorage
