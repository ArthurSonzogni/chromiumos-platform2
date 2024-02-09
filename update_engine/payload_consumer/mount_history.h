// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_PAYLOAD_CONSUMER_MOUNT_HISTORY_H_
#define UPDATE_ENGINE_PAYLOAD_CONSUMER_MOUNT_HISTORY_H_

#include "update_engine/payload_consumer/file_descriptor.h"

namespace chromeos_update_engine {
// Try to parse an ext4 from the partition specified by |blockdevice_fd|.
// If ext4 header exists and remount is detected, log mount count and date.
void LogMountHistory(const FileDescriptorPtr blockdevice_fd);
}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_PAYLOAD_CONSUMER_MOUNT_HISTORY_H_
