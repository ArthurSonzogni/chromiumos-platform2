// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_STORAGE_CONTAINER_RAMDISK_DEVICE_H_
#define LIBSTORAGE_STORAGE_CONTAINER_RAMDISK_DEVICE_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/backing_device.h"
#include "libstorage/storage_container/loopback_device.h"

namespace libstorage {

// RamdiskDevice is a variation of a loopback device, created on top
// of a tmpfs.
// The assumption is the |backing_file_path| given to the loopback device is
// the following format:
// /<tmpfs device>/directory/name.

class BRILLO_EXPORT RamdiskDevice final : public LoopbackDevice {
 public:
  ~RamdiskDevice() override = default;

  bool Create() override;
  bool Purge() override;
  bool Teardown() override;
  BackingDeviceType GetType() override { return LoopbackDevice::GetType(); }

  static std::unique_ptr<RamdiskDevice> Generate(
      const base::FilePath& backing_file_path, Platform* platform);

 private:
  RamdiskDevice(const BackingDeviceConfig& config, Platform* platform);

  Platform* platform_;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_STORAGE_CONTAINER_RAMDISK_DEVICE_H_
