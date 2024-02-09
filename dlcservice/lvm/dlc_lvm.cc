// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/lvm/dlc_lvm.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/stringprintf.h>
#include <lvmd/proto_bindings/lvmd.pb.h>
#include <lvmd/dbus-proxies.h>

#include "dlcservice/system_state.h"
#include "dlcservice/utils.h"
#include "dlcservice/utils/utils_interface.h"

namespace dlcservice {

DlcLvm::DlcLvm(DlcId id, std::shared_ptr<UtilsInterface> utils)
    : DlcBase(std::move(id), utils) {}

bool DlcLvm::CreateDlc(brillo::ErrorPtr* err) {
  if (!UseLogicalVolume()) {
    LOG(INFO) << "Skipping creation of logical volumes for DLC=" << id_;
    return DlcBase::CreateDlc(err);
  }

  LOG(INFO) << "Creating logical volumes for DLC=" << id_;
  if (!CreateDlcLogicalVolumes()) {
    LOG(ERROR) << "Failed to create logical volumes for DLC=" << id_;
    // TODO(b/236007986): Report metrics.
    *err = Error::Create(
        FROM_HERE, kErrorInternal,
        base::StringPrintf("Failed to create DLC=%s logical volumes.",
                           id_.c_str()));
    return false;
  }

  // TODO(b/236007986): Fix cros deploy'ing by migrating existing DLCs
  // pre-logical volume here.
  return true;
}

bool DlcLvm::CreateDlcLogicalVolumes() {
  std::vector<lvmd::LogicalVolumeConfiguration> lv_configs(2);
  lv_configs[0].set_name(utils_->LogicalVolumeName(id_, PartitionSlot::A));
  lv_configs[1].set_name(utils_->LogicalVolumeName(id_, PartitionSlot::B));
  auto prealloc_size = manifest_->preallocated_size();
  // Use the actual image size when preallocated_size set to `DEV_SIZE`.
  auto size =
      prealloc_size == kMagicDevSize ? manifest_->size() : prealloc_size;
  // Convert to MiB from bytes (round up).
  size = 1 + (size - 1) / (1024 * 1024);
  // Cannot pass in a value of 0, so set the lower bound to 1MiB.
  size = std::max<int64_t>(1, size);
  lv_configs[0].set_size(size);
  lv_configs[1].set_size(size);
  if (!SystemState::Get()->lvmd_wrapper()->CreateLogicalVolumes(lv_configs)) {
    LOG(ERROR) << "Failed to create logical volumes for DLC=" << id_;
    return false;
  }
  if (prealloc_size == kMagicDevSize &&
      !SystemState::Get()->lvmd_wrapper()->ResizeLogicalVolumes(lv_configs)) {
    LOG(ERROR) << "Failed to resize logical volumes for DLC=" << id_;
    return false;
  }
  return true;
}

bool DlcLvm::DeleteInternal(brillo::ErrorPtr* err) {
  if (!UseLogicalVolume()) {
    LOG(INFO) << "Skipping deletion of logical volumes for DLC=" << id_;
    return DlcBase::DeleteInternal(err);
  }

  LOG(INFO) << "Deleting logical volumes for DLC=" << id_;

  bool ret = true;
  if (!DeleteInternalLogicalVolumes()) {
    *err = Error::CreateInternal(
        FROM_HERE, error::kFailedInternal,
        base::StringPrintf("Failed to delete logical volumes for DLC=%s",
                           id_.c_str()));
    ret = false;
  }
  // Still run base `DeleteInternal()`.
  // This allows migration onto newer release to cleanup old paths.
  return DlcBase::DeleteInternal(err) && ret;
}

bool DlcLvm::DeleteInternalLogicalVolumes() {
  return SystemState::Get()->lvmd_wrapper()->RemoveLogicalVolumes({
      utils_->LogicalVolumeName(id_, PartitionSlot::A),
      utils_->LogicalVolumeName(id_, PartitionSlot::B),
  });
}

bool DlcLvm::MountInternal(std::string* mount_point, brillo::ErrorPtr* err) {
  if (!UseLogicalVolume()) {
    return DlcBase::MountInternal(mount_point, err);
  }
  imageloader::LoadDlcRequest request;
  request.set_id(id_);
  request.set_path(
      GetImagePath(SystemState::Get()->active_boot_slot()).value());
  request.set_package(package_);
  if (!SystemState::Get()->image_loader()->LoadDlc(request, mount_point,
                                                   nullptr,
                                                   /*timeout_ms=*/60 * 1000)) {
    *err = Error::CreateInternal(FROM_HERE, error::kFailedToMountImage,
                                 "Imageloader is unavailable for LoadDlc().");
    state_.set_last_error_code(Error::GetErrorCode(*err));
    return false;
  }
  if (mount_point->empty()) {
    *err = Error::CreateInternal(FROM_HERE, error::kFailedToMountImage,
                                 "Imageloader LoadDlc() call failed.");
    state_.set_last_error_code(Error::GetErrorCode(*err));
    return false;
  }
  return true;
}

bool DlcLvm::MakeReadyForUpdateInternal() const {
  if (!UseLogicalVolume()) {
    LOG(INFO) << "Skipping update ready marking of logical volume for DLC="
              << id_;
    return DlcBase::MakeReadyForUpdateInternal();
  }

  auto inactive_lv_name = utils_->LogicalVolumeName(
      id_, ToPartitionSlot(SystemState::Get()->inactive_boot_slot()));
  if (!SystemState::Get()->lvmd_wrapper()->ActivateLogicalVolume(
          inactive_lv_name)) {
    LOG(ERROR) << "Failed to activate inactive logical volumes for DLC=" << id_;
    return false;
  }
  return true;
}

bool DlcLvm::VerifyInternal(const base::FilePath& image_path,
                            std::vector<uint8_t>* image_sha256) {
  if (!UseLogicalVolume()) {
    LOG(INFO) << "Skipping verification of logical voluems for DLC=" << id_;
    return DlcBase::VerifyInternal(image_path, image_sha256);
  }

  if (!utils_->HashFile(image_path, manifest_->size(), image_sha256,
                        /*skip_size_check=*/true)) {
    LOG(ERROR) << "Failed to hash logical volume: " << image_path.value();
    return false;
  }

  return true;
}

base::FilePath DlcLvm::GetImagePath(BootSlot::Slot slot) const {
  if (!UseLogicalVolume()) {
    return DlcBase::GetImagePath(slot);
  }
  auto lv_name = utils_->LogicalVolumeName(id_, ToPartitionSlot(slot));
  return base::FilePath(
      SystemState::Get()->lvmd_wrapper()->GetLogicalVolumePath(lv_name));
}

bool DlcLvm::IsActiveImagePresent() const {
  if (!UseLogicalVolume()) {
    return DlcBase::IsActiveImagePresent();
  }

  auto active_lv_name = utils_->LogicalVolumeName(
      id_, ToPartitionSlot(SystemState::Get()->active_boot_slot()));
  return SystemState::Get()->lvmd_wrapper()->ActivateLogicalVolume(
      active_lv_name);
}

bool DlcLvm::UseLogicalVolume() const {
  return manifest_->use_logical_volume() &&
         SystemState::Get()->IsLvmStackEnabled();
}

}  // namespace dlcservice
