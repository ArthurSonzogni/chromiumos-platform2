// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/payload_consumer/mount_history.h"

#include <inttypes.h>

#include <string>
#include <vector>

#include <base/logging.h>
#include <base/time/time.h>

#include "update_engine/common/utils.h"

namespace chromeos_update_engine {
void LogMountHistory(const FileDescriptorPtr blockdevice_fd) {
  static constexpr ssize_t kBlockSize = 4096;

  if (blockdevice_fd == nullptr) {
    return;
  }

  brillo::Blob block0_buffer(kBlockSize);
  ssize_t bytes_read;

  if (!utils::PReadAll(blockdevice_fd, block0_buffer.data(), kBlockSize, 0,
                       &bytes_read)) {
    LOG(WARNING) << "PReadAll failed";
    return;
  }

  if (bytes_read != kBlockSize) {
    LOG(WARNING) << "Could not read an entire block";
    return;
  }

  // https://ext4.wiki.kernel.org/index.php/Ext4_Disk_Layout
  // Super block starts from block 0, offset 0x400
  //   0x2C: len32 Mount time
  //   0x30: len32 Write time
  //   0x34: len16 Number of mounts since the last fsck
  //   0x38: len16 Magic signature 0xEF53

  time_t mount_time =
      *reinterpret_cast<uint32_t*>(&block0_buffer[0x400 + 0x2C]);
  uint16_t mount_count =
      *reinterpret_cast<uint16_t*>(&block0_buffer[0x400 + 0x34]);
  uint16_t magic = *reinterpret_cast<uint16_t*>(&block0_buffer[0x400 + 0x38]);

  if (magic == 0xEF53) {
    if (mount_count > 0) {
      LOG(WARNING) << "Device was remounted R/W " << mount_count << " times. "
                   << "Last remount happened on "
                   << base::Time::FromTimeT(mount_time) << ".";
    }
  }
}
}  // namespace chromeos_update_engine
