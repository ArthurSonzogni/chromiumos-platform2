// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBSTORAGE_STORAGE_CONTAINER_PARTITION_DEVICE_H_
#define LIBSTORAGE_STORAGE_CONTAINER_PARTITION_DEVICE_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/backing_device.h"
#include "libstorage/storage_container/loopback_device.h"

namespace libstorage {

// PartitionDevice represent a block device or a partition of a block device.
// The device must already exists.
// The name must include /dev/. For instance "/dev/nvme0n1p3".
class BRILLO_EXPORT PartitionDevice final : public BackingDevice {
 public:
  PartitionDevice(const BackingDeviceConfig& config, Platform* platform);
  ~PartitionDevice() override = default;

  bool Create() override;
  bool Purge() override;
  bool Setup() override;
  bool Teardown() override;
  bool Exists() override;

  std::optional<base::FilePath> GetPath() override { return name_; }
  BackingDeviceType GetType() override { return BackingDeviceType::kPartition; }

 private:
  const base::FilePath name_;
  Platform* platform_;
  bool initialized_;
};

}  // namespace libstorage

#endif  // LIBSTORAGE_STORAGE_CONTAINER_PARTITION_DEVICE_H_
