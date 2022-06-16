// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lvmd/lvmd.h"

#include <string>

#include <base/files/file_path.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/errors/error.h>
#include <brillo/errors/error_codes.h>
#include <chromeos/dbus/lvmd/dbus-constants.h>

namespace lvmd {

namespace {

brillo::ErrorPtr CreateError(const base::Location& location,
                             const std::string& code,
                             const std::string& msg) {
  return brillo::Error::Create(location, brillo::errors::dbus::kDomain, code,
                               msg);
}

}  // namespace

Lvmd::Lvmd(std::unique_ptr<brillo::LogicalVolumeManager> lvm)
    : DBusServiceDaemon(kLvmdServiceName), lvm_(std::move(lvm)) {}

bool Lvmd::GetPhysicalVolume(brillo::ErrorPtr* error,
                             const std::string& in_device_path,
                             lvmd::PhysicalVolume* out_physical_volume) {
  auto opt_pv = lvm_->GetPhysicalVolume(base::FilePath(in_device_path));

  if (!opt_pv) {
    *error = CreateError(
        FROM_HERE, kErrorInternal,
        base::StringPrintf("Failed to GetPhysicalVolume on device path (%s)",
                           in_device_path.c_str()));
    return false;
  }

  out_physical_volume->set_device_path(opt_pv->GetPath().value());
  return true;
}

bool Lvmd::GetVolumeGroup(brillo::ErrorPtr* error,
                          const lvmd::PhysicalVolume& in_physical_volume,
                          lvmd::VolumeGroup* out_volume_group) {
  auto device_path = base::FilePath(in_physical_volume.device_path());
  auto pv = brillo::PhysicalVolume(device_path, {});
  auto opt_vg = lvm_->GetVolumeGroup(pv);

  if (!opt_vg) {
    *error =
        CreateError(FROM_HERE, kErrorInternal,
                    base::StringPrintf("Failed to GetVolumeGroup for pv (%s)",
                                       device_path.value().c_str()));
    return false;
  }

  out_volume_group->set_name(opt_vg->GetName());
  return true;
}

bool Lvmd::GetThinpool(brillo::ErrorPtr* error,
                       const lvmd::VolumeGroup& in_volume_group,
                       const std::string& in_thinpool_name,
                       lvmd::Thinpool* out_thinpool) {
  auto vg_name = in_volume_group.name();
  auto vg = brillo::VolumeGroup(vg_name, {});
  auto opt_thinpool = lvm_->GetThinpool(vg, in_thinpool_name);

  if (!opt_thinpool) {
    *error = CreateError(
        FROM_HERE, kErrorInternal,
        base::StringPrintf("Failed to GetThinpool for thinpool (%s) in vg (%s)",
                           in_thinpool_name.c_str(), vg_name.c_str()));
    return false;
  }

  *out_thinpool->mutable_volume_group() = in_volume_group;
  out_thinpool->set_name(opt_thinpool->GetName());

  int64_t total_bytes;
  if (!opt_thinpool->GetTotalSpace(&total_bytes)) {
    *error =
        CreateError(FROM_HERE, kErrorInternal,
                    base::StringPrintf(
                        "Failed to GetTotalSpace for thinpool (%s) in vg (%s)",
                        in_thinpool_name.c_str(), vg_name.c_str()));
    return false;
  }
  out_thinpool->set_total_bytes(total_bytes);

  int64_t free_bytes;
  if (!opt_thinpool->GetFreeSpace(&free_bytes)) {
    *error =
        CreateError(FROM_HERE, kErrorInternal,
                    base::StringPrintf(
                        "Failed to GetFreeSpace for thinpool (%s) in vg (%s)",
                        in_thinpool_name.c_str(), vg_name.c_str()));
    return false;
  }
  out_thinpool->set_free_bytes(free_bytes);

  return true;
}

bool Lvmd::ListLogicalVolumes(
    brillo::ErrorPtr* error,
    const lvmd::VolumeGroup& in_volume_group,
    lvmd::LogicalVolumeList* out_logical_volume_list) {
  auto vg = brillo::VolumeGroup(in_volume_group.name(), {});
  auto lvs = lvm_->ListLogicalVolumes(vg);

  for (auto& lv : lvs) {
    auto* new_lv = out_logical_volume_list->add_logical_volume();
    *new_lv->mutable_volume_group() = in_volume_group;
    new_lv->set_name(lv.GetName());
    // Iteration cannot be const in order to call `GetPath()`.
    new_lv->set_path(lv.GetPath().value());
  }

  return true;
}

bool Lvmd::GetLogicalVolume(brillo::ErrorPtr* error,
                            const lvmd::VolumeGroup& in_volume_group,
                            const std::string& in_logical_volume_name,
                            lvmd::LogicalVolume* out_logical_volume) {
  auto vg_name = in_volume_group.name();
  auto vg = brillo::VolumeGroup(vg_name, {});
  auto opt_lv = lvm_->GetLogicalVolume(vg, in_logical_volume_name);

  if (!opt_lv) {
    *error = CreateError(
        FROM_HERE, kErrorInternal,
        base::StringPrintf("Failed to GetLogicalVolume for lv (%s) in vg (%s)",
                           in_logical_volume_name.c_str(), vg_name.c_str()));
    return false;
  }

  *out_logical_volume->mutable_volume_group() = in_volume_group;
  out_logical_volume->set_name(opt_lv->GetName());
  out_logical_volume->set_path(opt_lv->GetPath().value());
  return true;
}

int Lvmd::OnInit() {
  int return_code = brillo::DBusServiceDaemon::OnInit();
  if (return_code != EX_OK)
    return return_code;

  PostponeShutdown();
  return EX_OK;
}

void Lvmd::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  dbus_object_.reset(new brillo::dbus_utils::DBusObject(
      nullptr, bus_, org::chromium::LvmdAdaptor::GetObjectPath()));

  dbus_adaptor_.RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed.", true));
}

void Lvmd::OnShutdown(int* return_code) {
  brillo::DBusServiceDaemon::OnShutdown(return_code);
}

void Lvmd::PostponeShutdown() {
  shutdown_callback_.Reset(
      base::BindRepeating(&brillo::Daemon::Quit, weak_factory_.GetWeakPtr()));
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, shutdown_callback_.callback(), kShutdownTimeout);
}

}  // namespace lvmd
