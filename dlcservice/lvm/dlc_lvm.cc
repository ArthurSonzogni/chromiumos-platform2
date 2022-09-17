// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/lvm/dlc_lvm.h"

#include <utility>

#include <lvmd/proto_bindings/lvmd.pb.h>
#include <lvmd/dbus-proxies.h>

#include "dlcservice/lvm/lvm_utils.h"
#include "dlcservice/system_state.h"

namespace dlcservice {

DlcLvm::DlcLvm(DlcId id) : DlcBase(std::move(id)) {}

bool DlcLvm::CreateDlc(brillo::ErrorPtr* err) {
  if (manifest_->use_logical_volume()) {
    LOG(INFO) << "Using logical volumes for DLC=" << id_;
    // 1. Create logical volumes.
    if (!CreateDlcLogicalVolumes()) {
      LOG(ERROR) << "Failed to create logical volumes for DLC=" << id_;
      // TODO(b/236007986): Report metrics.
    }

    // TODO(b/236007986)
    // 1. Migrate existing encrypted stateful images into logical volumes.
    //   - This strongly depends on the fact that all other daemons are onboard
    //   with the idea of mounting/updating logical volumes now.
  } else {
    LOG(INFO) << "Skipping usage of logical volumes for DLC=" << id_;
  }

  // TODO(b/236007986): All dependent daemons/tests must migrate over to using
  // logical volume paths. Hence fallthrough here.
  return DlcBase::CreateDlc(err);
}

bool DlcLvm::CreateDlcLogicalVolumes() {
  lvmd::LogicalVolumeConfiguration lv_config_a, lv_config_b;
  lv_config_a.set_name(LogicalVolumeName(id_, BootSlotInterface::Slot::A));
  lv_config_b.set_name(LogicalVolumeName(id_, BootSlotInterface::Slot::B));
  auto size = manifest_->preallocated_size();
  // Convert to MiB from bytes.
  size /= 1024 * 1024;
  lv_config_a.set_size(size);
  lv_config_b.set_size(size);
  if (!SystemState::Get()->lvmd_wrapper()->CreateLogicalVolumes({
          lv_config_a,
          lv_config_b,
      })) {
    LOG(ERROR) << "Failed to create logical volumes for DLC=" << id_;
    return false;
  }
  return true;
}

bool DlcLvm::DeleteInternal(brillo::ErrorPtr* err) {
  bool ret = true;
  if (manifest_->use_logical_volume() && !DeleteInternalLogicalVolumes()) {
    LOG(ERROR) << "Failed to delete logical volumes for DLC=" << id_;
    ret = false;
  }
  // Still run base `DeleteInternal()`.
  return DlcBase::DeleteInternal(err) && ret;
}

bool DlcLvm::DeleteInternalLogicalVolumes() {
  if (!SystemState::Get()->lvmd_wrapper()->RemoveLogicalVolumes({
          LogicalVolumeName(id_, BootSlotInterface::Slot::A),
          LogicalVolumeName(id_, BootSlotInterface::Slot::B),
      })) {
    LOG(ERROR) << "Failed to remove logical volumes for DLC=" << id_;
    return false;
  }
  return true;
}

}  // namespace dlcservice
