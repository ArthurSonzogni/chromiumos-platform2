// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libstorage/storage_container/loopback_device.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/values.h>
#include <libstorage/platform/platform.h>

#include "libstorage/storage_container/backing_device.h"

namespace libstorage {

LoopbackDevice::LoopbackDevice(const BackingDeviceConfig& config,
                               Platform* platform)
    : backing_file_path_(config.loopback.backing_file_path),
      name_(config.name),
      size_(config.size),
      platform_(platform) {}

bool LoopbackDevice::Create() {
  if (!platform_->CreateSparseFile(backing_file_path_, size_) ||
      !platform_->SetPermissions(backing_file_path_, S_IRUSR | S_IWUSR)) {
    LOG(ERROR) << "Failed to create sparse file.";
    return false;
  }
  return true;
}

bool LoopbackDevice::Purge() {
  return platform_->DeleteFile(backing_file_path_);
}

bool LoopbackDevice::Setup() {
  // Check the size of the sparse file and resize if necessary.
  // It may have been created small and resized slowly to improve boot time.
  // Resizing the sparse file via truncate() should be a no-op.
  base::File file;
  platform_->InitializeFile(&file, backing_file_path_,
                            base::File::FLAG_OPEN | base::File::FLAG_WRITE);

  if (!file.IsValid()) {
    LOG(ERROR) << "Unable to open backing device";
    return false;
  }
  if (file.GetLength() < size_) {
    LOG(INFO) << "Expanding underlying sparse file to " << size_;
    file.SetLength(size_);
  }

  // Set up loopback device.
  std::unique_ptr<brillo::LoopDevice> loopdev =
      platform_->GetLoopDeviceManager()->AttachDeviceToFile(backing_file_path_);

  if (!loopdev->IsValid()) {
    LOG(ERROR) << "Failed to attach loop back device";
    return false;
  }

  // Set loop device name.
  if (!loopdev->SetName(name_)) {
    LOG(ERROR) << "Loop set name failed";
    loopdev->Detach();
    return false;
  }

  return true;
}

bool LoopbackDevice::Teardown() {
  std::unique_ptr<brillo::LoopDevice> loopdev =
      platform_->GetLoopDeviceManager()->GetAttachedDeviceByName(name_);

  if (!loopdev->IsValid()) {
    LOG(ERROR) << "Loop device does not exist.";
    return false;
  }

  std::ignore = loopdev->SetName("");

  return loopdev->Detach();
}

bool LoopbackDevice::Exists() {
  return platform_->FileExists(backing_file_path_);
}

std::optional<base::FilePath> LoopbackDevice::GetPath() {
  std::unique_ptr<brillo::LoopDevice> loopdev =
      platform_->GetLoopDeviceManager()->GetAttachedDeviceByName(name_);

  if (!loopdev->IsValid()) {
    LOG(ERROR) << "Loop device does not exist.";
    return std::nullopt;
  }

  return loopdev->GetDevicePath();
}

}  // namespace libstorage
