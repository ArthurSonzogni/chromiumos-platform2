// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/ramdisk_device.h"

#include <linux/magic.h>
#include <sys/statfs.h>

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <libstorage/platform/platform.h>

#include "cryptohome/storage/encrypted_container/loopback_device.h"

namespace cryptohome {
RamdiskDevice::RamdiskDevice(const BackingDeviceConfig& config,

                             libstorage::Platform* platform)
    : LoopbackDevice(config, platform), platform_(platform) {}

bool RamdiskDevice::Create() {
  if (!platform_->CreateDirectory(backing_file_path_.DirName())) {
    LOG(ERROR) << "Can't create directory for ephemeral backing file";
    return false;
  }
  return LoopbackDevice::Create();
}

bool RamdiskDevice::Teardown() {
  bool ok = LoopbackDevice::Teardown();
  if (!platform_->DeleteFileDurable(backing_file_path_)) {
    LOG(ERROR) << "Can't delete ephemeral file";
    return false;
  }
  return ok;
}

bool RamdiskDevice::Purge() {
  bool ok = LoopbackDevice::Purge();
  if (!platform_->DeleteFileDurable(backing_file_path_)) {
    LOG(ERROR) << "Can't delete ephemeral file";
    return false;
  }
  return ok;
}

std::unique_ptr<RamdiskDevice> RamdiskDevice::Generate(
    const base::FilePath& backing_file_path, libstorage::Platform* platform) {
  // Determine ephemeral cryptohome size.
  struct statfs fs;
  if (!platform->StatFS(base::FilePath(backing_file_path.DirName().DirName()),
                        &fs)) {
    PLOG(ERROR) << "Can't determine size for ephemeral device";
    return nullptr;
  }

  if (fs.f_type != TMPFS_MAGIC) {
    LOG(ERROR) << "The backing file is not over tmpfs";
    return nullptr;
  }

  const int64_t sparse_size = static_cast<int64_t>(fs.f_blocks * fs.f_frsize);

  BackingDeviceConfig config{
      .type = BackingDeviceType::kLoopbackDevice,
      .name = "ephemeral",
      .size = sparse_size,
      .loopback =
          {
              .backing_file_path = backing_file_path,
          },
  };

  return std::unique_ptr<RamdiskDevice>(new RamdiskDevice(config, platform));
}

}  // namespace cryptohome
