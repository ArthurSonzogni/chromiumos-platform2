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
    const BackingDeviceConfig& config,
    std::unique_ptr<brillo::LogicalVolumeManager> lvm)
    : name_(config.name),
      size_(config.size),
      physical_volume_(config.logical_volume.physical_volume),
      thinpool_name_(config.logical_volume.thinpool_name),
      lvm_(std::move(lvm)) {}

LogicalVolumeBackingDevice::LogicalVolumeBackingDevice(
    const BackingDeviceConfig& config)
    : LogicalVolumeBackingDevice(
          config, std::make_unique<brillo::LogicalVolumeManager>()) {}

std::optional<brillo::LogicalVolume>
LogicalVolumeBackingDevice::GetLogicalVolume() {
  std::optional<brillo::PhysicalVolume> pv =
      lvm_->GetPhysicalVolume(physical_volume_);

  if (!pv || !pv->IsValid()) {
    return std::nullopt;
  }

  std::optional<brillo::VolumeGroup> vg = lvm_->GetVolumeGroup(*pv);

  if (!vg || !vg->IsValid()) {
    return std::nullopt;
  }

  return lvm_->GetLogicalVolume(*vg, name_);
}

bool LogicalVolumeBackingDevice::Purge() {
  std::optional<brillo::LogicalVolume> lv = GetLogicalVolume();

  if (lv && lv->IsValid()) {
    return lv->Remove();
  }

  return false;
}

bool LogicalVolumeBackingDevice::Create() {
  std::optional<brillo::PhysicalVolume> pv =
      lvm_->GetPhysicalVolume(physical_volume_);

  if (!pv || !pv->IsValid()) {
    return false;
  }

  std::optional<brillo::VolumeGroup> vg = lvm_->GetVolumeGroup(*pv);

  if (!vg || !vg->IsValid()) {
    return false;
  }

  std::optional<brillo::Thinpool> thinpool =
      lvm_->GetThinpool(*vg, thinpool_name_);
  if (!thinpool || !thinpool->IsValid()) {
    return false;
  }

  base::Value lv_config(base::Value::Type::DICTIONARY);
  lv_config.SetStringKey("name", name_);
  lv_config.SetStringKey("size", base::NumberToString(size_));

  std::optional<brillo::LogicalVolume> lv =
      lvm_->CreateLogicalVolume(*vg, *thinpool, lv_config);
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
