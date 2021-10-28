// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/backing_device_factory.h"

#include <memory>

#include "cryptohome/platform.h"
#include "cryptohome/storage/encrypted_container/backing_device.h"
#include "cryptohome/storage/encrypted_container/ramdisk_device.h"
// TODO(b/177929620): Cleanup once lvm utils are built unconditionally.
#if USE_LVM_STATEFUL_PARTITION
#include "cryptohome/storage/encrypted_container/logical_volume_backing_device.h"
#endif  // USE_LVM_STATEFUL_PARTITION

namespace cryptohome {

BackingDeviceFactory::BackingDeviceFactory(Platform* platform)
    : platform_(platform) {}

std::unique_ptr<BackingDevice> BackingDeviceFactory::Generate(
    const BackingDeviceConfig& config) {
  switch (config.type) {
    case BackingDeviceType::kLoopbackDevice:
      return std::make_unique<LoopbackDevice>(config, platform_);
    case BackingDeviceType::kRamdiskDevice:
      return RamdiskDevice::Generate(config.ramdisk.backing_file_name,
                                     platform_);
// TODO(b/177929620): Cleanup once lvm utils are built unconditionally.
#if USE_LVM_STATEFUL_PARTITION
    case BackingDeviceType::kLogicalVolumeBackingDevice:
      return std::make_unique<LogicalVolumeBackingDevice>(config);
#endif  // USE_LVM_STATEFUL_PARTITION
    default:
      return nullptr;
  }
}

}  // namespace cryptohome
