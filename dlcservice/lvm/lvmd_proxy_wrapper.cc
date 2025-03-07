// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/lvm/lvmd_proxy_wrapper.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <lvmd/proto_bindings/lvmd.pb.h>

#include "dlcservice/system_state.h"

// TODO(b/254557435): Reduce the # of calls made into lvmd.
namespace dlcservice {
namespace {

// CrOS currently only uses "thinpool" as thinpool name.
constexpr char kThinpoolName[] = "thinpool";

// The numeric group value for `disk-dlc`.
constexpr int kDiskDlcGid = 20777;

}  // namespace

LvmdProxyWrapper::LvmdProxyWrapper(
    std::unique_ptr<org::chromium::LvmdProxyInterface> lvmd_proxy)
    : LvmdProxyWrapper(std::move(lvmd_proxy), std::make_unique<Utils>()) {}

LvmdProxyWrapper::LvmdProxyWrapper(
    std::unique_ptr<org::chromium::LvmdProxyInterface> lvmd_proxy,
    std::unique_ptr<UtilsInterface> utils)
    : lvmd_proxy_(std::move(lvmd_proxy)), utils_(std::move(utils)) {}

bool LvmdProxyWrapper::GetPhysicalVolume(const std::string& device_path,
                                         lvmd::PhysicalVolume* pv) {
  brillo::ErrorPtr err;
  if (!lvmd_proxy_->GetPhysicalVolume(device_path, pv, &err)) {
    LOG(WARNING) << "Failed to GetPhysicalVolume from lvmd: "
                 << Error::ToString(err);
    return false;
  }
  return true;
}

bool LvmdProxyWrapper::GetVolumeGroup(const lvmd::PhysicalVolume& pv,
                                      lvmd::VolumeGroup* vg) {
  brillo::ErrorPtr err;
  if (!lvmd_proxy_->GetVolumeGroup(pv, vg, &err)) {
    LOG(WARNING) << "Failed to GetVolumeGroup from lvmd: "
                 << Error::ToString(err);
    return false;
  }
  return true;
}

bool LvmdProxyWrapper::GetThinpool(const lvmd::VolumeGroup& vg,
                                   lvmd::Thinpool* thinpool) {
  brillo::ErrorPtr err;
  if (!lvmd_proxy_->GetThinpool(vg, kThinpoolName, thinpool, &err)) {
    LOG(WARNING) << "Failed to GetThinpool from lvmd: " << Error::ToString(err);
    return false;
  }
  return true;
}

bool LvmdProxyWrapper::GetLogicalVolume(const lvmd::VolumeGroup& vg,
                                        const std::string& lv_name,
                                        lvmd::LogicalVolume* lv) {
  brillo::ErrorPtr err;
  if (!lvmd_proxy_->GetLogicalVolume(vg, lv_name, lv, &err)) {
    LOG(WARNING) << "Failed to GetLogicalVolume from lvmd: "
                 << Error::ToString(err);
    return false;
  }
  return true;
}

bool LvmdProxyWrapper::GetLogicalVolume(const std::string& lv_name,
                                        lvmd::LogicalVolume* lv) {
  auto stateful_path =
      SystemState::Get()->boot_slot()->GetStatefulPartitionPath();

  if (stateful_path.empty()) {
    LOG(ERROR) << "Failed to GetStatefulPartitionPath.";
    return false;
  }

  lvmd::PhysicalVolume pv;
  if (!GetPhysicalVolume(stateful_path.value(), &pv)) {
    LOG(ERROR) << "Failed to GetPhysicalVolume.";
    return false;
  }

  lvmd::VolumeGroup vg;
  if (!GetVolumeGroup(pv, &vg)) {
    LOG(ERROR) << "Failed to GetVolumeGroup.";
    return false;
  }

  brillo::ErrorPtr err;
  if (!lvmd_proxy_->GetLogicalVolume(vg, lv_name, lv, &err)) {
    LOG(ERROR) << "Failed to GetLogicalVolume.";
    return false;
  }

  return true;
}

bool LvmdProxyWrapper::CreateLogicalVolume(
    const lvmd::Thinpool& thinpool,
    const lvmd::LogicalVolumeConfiguration& lv_config,
    lvmd::LogicalVolume* lv) {
  brillo::ErrorPtr err;
  if (!lvmd_proxy_->CreateLogicalVolume(thinpool, lv_config, lv, &err)) {
    LOG(WARNING) << "Failed to CreateLogicalVolume in lvmd: "
                 << Error::ToString(err);
    return false;
  }
  // Check for permission changes, this is to handle race condition with
  // current DLC udev rules.
  const auto& lv_mapper_path =
      utils_->MakeAbsoluteFilePath(base::FilePath{lv->path()});
  if (!utils_->WaitForGid(lv_mapper_path, kDiskDlcGid)) {
    LOG(ERROR)
        << "Failed to CreateLogicalVolume as udev rules did not run for path="
        << lv_mapper_path;
    return false;
  }
  return true;
}

bool LvmdProxyWrapper::ToggleLogicalVolumeActivation(
    const lvmd::LogicalVolume& lv, bool activate) {
  brillo::ErrorPtr err;
  if (!lvmd_proxy_->ToggleLogicalVolumeActivation(lv, activate, &err)) {
    LOG(WARNING) << "Failed to ToggleLogicalVolumeActivation in lvmd: "
                 << Error::ToString(err);
    return false;
  }
  return true;
}

bool LvmdProxyWrapper::CreateLogicalVolumes(
    const std::vector<lvmd::LogicalVolumeConfiguration>& lv_configs) {
  auto stateful_path =
      SystemState::Get()->boot_slot()->GetStatefulPartitionPath();

  if (stateful_path.empty()) {
    LOG(ERROR) << "Failed to GetStatefulPartitionPath.";
    return false;
  }

  lvmd::PhysicalVolume pv;
  if (!GetPhysicalVolume(stateful_path.value(), &pv)) {
    LOG(ERROR) << "Failed to GetPhysicalVolume.";
    return false;
  }

  lvmd::VolumeGroup vg;
  if (!GetVolumeGroup(pv, &vg)) {
    LOG(ERROR) << "Failed to GetVolumeGroup.";
    return false;
  }

  lvmd::Thinpool thinpool;
  if (!GetThinpool(vg, &thinpool)) {
    LOG(ERROR) << "Failed to GetThinpool.";
    return false;
  }

  // Prefer using thinpool's volume group as thinpool is passed into creating
  // the logical volumes.
  lvmd::LogicalVolume lv;
  for (const auto& lv_config : lv_configs) {
    auto lv_name = lv_config.name();
    if (GetLogicalVolume(thinpool.volume_group(), lv_name, &lv)) {
      if (!ToggleLogicalVolumeActivation(lv, /*activate=*/true)) {
        LOG(ERROR) << "Failed to ToggleLogicalVolumeActivation name="
                   << lv_name;
        return false;
      }
      LOG(INFO) << "Activated name=" << lv_name;
    } else if (!CreateLogicalVolume(thinpool, lv_config, &lv)) {
      LOG(ERROR) << "Failed to CreateLogicalVolume name=" << lv_name;
      return false;
    }
  }
  return true;
}

bool LvmdProxyWrapper::RemoveLogicalVolumes(
    const std::vector<std::string>& lv_names) {
  auto stateful_path =
      SystemState::Get()->boot_slot()->GetStatefulPartitionPath();

  lvmd::PhysicalVolume pv;
  if (!GetPhysicalVolume(stateful_path.value(), &pv)) {
    LOG(ERROR) << "Failed to GetPhysicalVolume.";
    return false;
  }

  lvmd::VolumeGroup vg;
  if (!GetVolumeGroup(pv, &vg)) {
    LOG(ERROR) << "Failed to GetVolumeGroup.";
    return false;
  }

  brillo::ErrorPtr err;
  lvmd::RemoveLogicalVolumesRequest request;
  lvmd::RemoveLogicalVolumesResponse response;

  for (const auto& lv_name : lv_names) {
    auto* lv = request.mutable_logical_volume_list()->add_logical_volume();
    lv->set_name(lv_name);
    lv->mutable_volume_group()->CopyFrom(vg);
  }
  if (!lvmd_proxy_->RemoveLogicalVolumes(request, &response, &err)) {
    LOG(WARNING) << "Failed to RemoveLogicalVolumes in lvmd: "
                 << Error::ToString(err);
    return false;
  }
  return true;
}

void LvmdProxyWrapper::RemoveLogicalVolumesAsync(
    const std::vector<std::string>& lv_names,
    base::OnceCallback<void(bool)> cb) {
  lvmd::RemoveLogicalVolumesRequest request;
  auto* lv_list = request.mutable_logical_volume_list();
  for (const auto& lv_name : lv_names) {
    auto* lv = lv_list->add_logical_volume();
    lv->set_name(lv_name);
  }
  auto [cb1, cb2] = base::SplitOnceCallback(std::move(cb));
  lvmd_proxy_->RemoveLogicalVolumesAsync(
      request,
      base::BindOnce(
          [](decltype(cb) cb, const lvmd::RemoveLogicalVolumesResponse&) {
            std::move(cb).Run(true);
          },
          std::move(cb1)),
      base::BindOnce(
          [](decltype(cb) cb, brillo::Error*) { std::move(cb).Run(false); },
          std::move(cb2)));
}

bool LvmdProxyWrapper::ActivateLogicalVolume(const std::string& lv_name) {
  auto stateful_path =
      SystemState::Get()->boot_slot()->GetStatefulPartitionPath();

  lvmd::PhysicalVolume pv;
  if (!GetPhysicalVolume(stateful_path.value(), &pv)) {
    LOG(ERROR) << "Failed to GetPhysicalVolume.";
    return false;
  }

  lvmd::VolumeGroup vg;
  if (!GetVolumeGroup(pv, &vg)) {
    LOG(ERROR) << "Failed to GetVolumeGroup.";
    return false;
  }

  lvmd::LogicalVolume lv;
  if (!GetLogicalVolume(vg, lv_name, &lv)) {
    LOG(ERROR) << "Failed to GetLogicalVolume.";
    return false;
  }

  if (!ToggleLogicalVolumeActivation(lv, /*activate=*/true)) {
    LOG(ERROR) << "Failed to ToggleLogicalVolumeActivation from lvmd.";
    return false;
  }
  return true;
}

bool LvmdProxyWrapper::ListLogicalVolumes(lvmd::LogicalVolumeList* lvs) {
  auto stateful_path =
      SystemState::Get()->boot_slot()->GetStatefulPartitionPath();

  lvmd::PhysicalVolume pv;
  if (!GetPhysicalVolume(stateful_path.value(), &pv)) {
    LOG(ERROR) << "Failed to GetPhysicalVolume.";
    return false;
  }

  lvmd::VolumeGroup vg;
  if (!GetVolumeGroup(pv, &vg)) {
    LOG(ERROR) << "Failed to GetVolumeGroup.";
    return false;
  }

  brillo::ErrorPtr err;
  return lvmd_proxy_->ListLogicalVolumes(vg, lvs, &err);
}

std::string LvmdProxyWrapper::GetLogicalVolumePath(const std::string& lv_name) {
  lvmd::LogicalVolume lv;
  return GetLogicalVolume(lv_name, &lv) ? lv.path() : "";
}

std::optional<int64_t> LvmdProxyWrapper::GetLogicalVolumeSize(
    const std::string& lv_name) {
  lvmd::LogicalVolume lv;
  if (GetLogicalVolume(lv_name, &lv) && lv.size() >= 0) {
    return lv.size();
  }
  return std::nullopt;
}

bool LvmdProxyWrapper::ResizeLogicalVolumes(
    const std::vector<lvmd::LogicalVolumeConfiguration>& lv_configs) {
  auto stateful_path =
      SystemState::Get()->boot_slot()->GetStatefulPartitionPath();

  if (stateful_path.empty()) {
    LOG(ERROR) << "Failed to GetStatefulPartitionPath.";
    return false;
  }

  lvmd::PhysicalVolume pv;
  if (!GetPhysicalVolume(stateful_path.value(), &pv)) {
    LOG(ERROR) << "Failed to GetPhysicalVolume.";
    return false;
  }

  lvmd::VolumeGroup vg;
  if (!GetVolumeGroup(pv, &vg)) {
    LOG(ERROR) << "Failed to GetVolumeGroup.";
    return false;
  }

  lvmd::LogicalVolume lv;
  for (const auto& lv_config : lv_configs) {
    auto lv_name = lv_config.name();
    if (!GetLogicalVolume(vg, lv_name, &lv)) {
      LOG(ERROR) << "Failed to GetLogicalVolume " << lv_name;
      return false;
    }

    brillo::ErrorPtr err;
    if (!lvmd_proxy_->ResizeLogicalVolume(lv, lv_config.size(), &err)) {
      LOG(ERROR) << "Failed to ResizeLogicalVolume from lvmd: "
                 << Error::ToString(err);
      return false;
    }
  }
  return true;
}

}  // namespace dlcservice
