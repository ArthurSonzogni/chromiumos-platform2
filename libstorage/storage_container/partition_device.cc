// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libstorage/storage_container/partition_device.h"

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <absl/cleanup/cleanup.h>
#include <base/files/file_path.h>
#include <base/functional/callback_helpers.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/storage_container.h"

namespace libstorage {

PartitionDevice::PartitionDevice(const BackingDeviceConfig& config,
                                 Platform* platform)
    : name_(config.name), platform_(platform), initialized_(false) {}

// We assume partitions have already been created (via LVM or/and kernel
// block and partition discovery). We should not have to create one.
// We can only be called by mistake (invalid partition name, broken SSD).
bool PartitionDevice::Create() {
  LOG(ERROR) << "Unexpected creation request for " << name_;
  return false;
}

// Since we are not creatng partition, Purge should never be called.
bool PartitionDevice::Purge() {
  DCHECK(false) << "unsupported";
  return false;
}

// Setup does nothing, but should not be called twice in a row.
bool PartitionDevice::Setup() {
  DCHECK(!initialized_) << "Setup for " << name_ << " already called";
  initialized_ = true;
  return Exists();
}

bool PartitionDevice::Teardown() {
  initialized_ = false;
  return true;
}

bool PartitionDevice::Exists() {
  if (!platform_->FileExists(name_))
    return false;

  base::stat_wrapper_t statbuf;
  if (!platform_->Stat(name_, &statbuf))
    return false;

  return S_ISBLK(statbuf.st_mode);
}

}  // namespace libstorage
