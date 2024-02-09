// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_RAMDISK_DEVICE_H_
#define CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_RAMDISK_DEVICE_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/storage/encrypted_container/backing_device.h"
#include "cryptohome/storage/encrypted_container/loopback_device.h"

namespace cryptohome {

// RamdiskDevice is a variation of a loopback device, created on top
// of a tmpfs.
// The assumption is the |backing_file_path| given to the loopback device is
// the follwing format:
// /<tmpfs device>/directory/name.

class RamdiskDevice final : public LoopbackDevice {
 public:
  ~RamdiskDevice() override = default;

  bool Create() override;
  bool Purge() override;
  bool Teardown() override;
  BackingDeviceType GetType() override { return LoopbackDevice::GetType(); }

  static std::unique_ptr<RamdiskDevice> Generate(
      const base::FilePath& backing_file_path, libstorage::Platform* platform);

 private:
  RamdiskDevice(const BackingDeviceConfig& config,
                libstorage::Platform* platform);

  libstorage::Platform* platform_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_RAMDISK_DEVICE_H_
