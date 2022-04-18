// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/encrypted_container/logical_volume_backing_device.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/values.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/blkdev_utils/lvm_device.h>

#include "cryptohome/storage/encrypted_container/backing_device.h"

namespace cryptohome {

LogicalVolumeBackingDevice::LogicalVolumeBackingDevice(
    const BackingDeviceConfig& config, brillo::LogicalVolumeManager* lvm)
    : name_(config.name),
      size_(config.size),
      vg_(config.logical_volume.vg),
      thinpool_(config.logical_volume.thinpool),
      lvm_(lvm) {}

std::optional<brillo::LogicalVolume>
LogicalVolumeBackingDevice::GetLogicalVolume() {
  return lvm_->GetLogicalVolume(*vg_.get(), name_);
}

bool LogicalVolumeBackingDevice::Purge() {
  std::optional<brillo::LogicalVolume> lv = GetLogicalVolume();

  if (lv && lv->IsValid()) {
    return lv->Remove();
  }

  return false;
}

bool LogicalVolumeBackingDevice::Create() {
  base::Value lv_config(base::Value::Type::DICTIONARY);
  lv_config.SetStringKey("name", name_);
  lv_config.SetStringKey("size", base::NumberToString(size_));

  std::optional<brillo::LogicalVolume> lv =
      lvm_->CreateLogicalVolume(*vg_.get(), *thinpool_.get(), lv_config);
  if (!lv || !lv->IsValid()) {
    return false;
  }

  return true;
}

bool LogicalVolumeBackingDevice::Setup() {
  std::optional<brillo::LogicalVolume> lv = GetLogicalVolume();

  if (!lv || !lv->IsValid()) {
    LOG(ERROR) << "Failed to set up logical volume.";
    return false;
  }

  return lv->Activate();
}

bool LogicalVolumeBackingDevice::Teardown() {
  std::optional<brillo::LogicalVolume> lv = GetLogicalVolume();

  if (!lv || !lv->IsValid()) {
    LOG(ERROR) << "Invalid logical volume";
    return false;
  }

  return lv->Deactivate();
}

bool LogicalVolumeBackingDevice::Exists() {
  std::optional<brillo::LogicalVolume> lv = GetLogicalVolume();

  return lv && lv->IsValid();
}

std::optional<base::FilePath> LogicalVolumeBackingDevice::GetPath() {
  std::optional<brillo::LogicalVolume> lv = GetLogicalVolume();

  if (!lv || !lv->IsValid()) {
    LOG(ERROR) << "Invalid logical volume";
    return std::nullopt;
  }

  return lv->GetPath();
}

}  // namespace cryptohome
