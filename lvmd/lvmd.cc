// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lvmd/lvmd.h"

#include <cstdint>
#include <string>
#include <vector>

#include <brillo/userdb_utils.h>
#include <dbus/object_proxy.h>

#include <base/files/file_path.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_runner.h>
#include <brillo/errors/error.h>
#include <brillo/errors/error_codes.h>
#include <brillo/strings/string_utils.h>
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
  out_thinpool->set_name(opt_thinpool->GetRawName());

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
    new_lv->set_name(lv.GetRawName());
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
  out_logical_volume->set_name(opt_lv->GetRawName());
  out_logical_volume->set_path(opt_lv->GetPath().value());
  if (auto size = opt_lv->GetSize()) {
    out_logical_volume->set_size(*size);
  } else {
    out_logical_volume->set_size(kErrorSize);
  }
  return true;
}

bool Lvmd::CreateLogicalVolume(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    const lvmd::Thinpool& in_thinpool,
    const lvmd::LogicalVolumeConfiguration& in_logical_volume_configuration,
    lvmd::LogicalVolume* out_logical_volume) {
  auto lv_name = in_logical_volume_configuration.name();
  if (!IsClientAuthorized(error, message, lv_name)) {
    return false;
  }

  auto vg_name = in_thinpool.volume_group().name();
  auto vg = brillo::VolumeGroup(vg_name, {});

  auto thinpool_name = in_thinpool.name();
  auto thinpool = brillo::Thinpool(thinpool_name, vg_name, {});

  base::DictValue config;
  config.Set("name", lv_name);
  config.Set("size", brillo::string_utils::ToString(
                         in_logical_volume_configuration.size()));

  auto opt_lv = lvm_->CreateLogicalVolume(vg, thinpool, config);

  if (!opt_lv) {
    *error =
        CreateError(FROM_HERE, kErrorInternal,
                    base::StringPrintf("Failed to CreateLogicalVolume for lv "
                                       "name (%s) in thinpool (%s) in vg (%s)",
                                       lv_name.c_str(), thinpool_name.c_str(),
                                       vg_name.c_str()));
    return false;
  }

  *out_logical_volume->mutable_volume_group() = in_thinpool.volume_group();
  out_logical_volume->set_name(opt_lv->GetRawName());
  out_logical_volume->set_path(opt_lv->GetPath().value());
  return true;
}

bool Lvmd::CreateLogicalVolumes(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    const lvmd::CreateLogicalVolumesRequest& in_request,
    lvmd::CreateLogicalVolumesResponse* out_response) {
  for (const auto& info : in_request.logical_volume_infos()) {
    if (!IsClientAuthorized(error, message, info.lv_config().name())) {
      return false;
    }
  }

  std::vector<std::string> failed_lvs;
  for (const auto& info : in_request.logical_volume_infos()) {
    const auto& thinpool = info.thinpool();
    const auto& lv_config = info.lv_config();

    brillo::ErrorPtr tmp_err;
    lvmd::LogicalVolume lv;
    if (!CreateLogicalVolume(&tmp_err, message, thinpool, lv_config, &lv)) {
      failed_lvs.push_back(base::StringPrintf(
          "lv name (%s) thinpool (%s) vg (%s) size (%ld)",
          lv_config.name().c_str(), thinpool.name().c_str(),
          thinpool.volume_group().name().c_str(), lv_config.size()));
      // Continue looping to handle remaining logical volumes to create.
      continue;
    }
    // Only add successfully created logical volumes into the response.
    auto* lv_added =
        out_response->mutable_logical_volume_list()->add_logical_volume();
    lv_added->Swap(&lv);
  }

  if (!failed_lvs.empty()) {
    *error = CreateError(
        FROM_HERE, kErrorInternal,
        base::StringPrintf("Failed to CreateLogicalVolumes: %s",
                           base::JoinString(failed_lvs, ", ").c_str()));
    return false;
  }
  return true;
}

bool Lvmd::RemoveLogicalVolume(brillo::ErrorPtr* error,
                               dbus::Message* message,
                               const lvmd::LogicalVolume& in_logical_volume) {
  std::string lv_name = in_logical_volume.name();
  if (!IsClientAuthorized(error, message, lv_name)) {
    return false;
  }

  auto vg_name = in_logical_volume.volume_group().name();
  auto vg = brillo::VolumeGroup(vg_name, {});

  if (!lvm_->RemoveLogicalVolume(vg, lv_name)) {
    *error =
        CreateError(FROM_HERE, kErrorInternal,
                    base::StringPrintf("Failed to RemoveLogicalVolume for lv "
                                       "name (%s) in vg (%s)",
                                       lv_name.c_str(), vg_name.c_str()));
    return false;
  }

  return true;
}

bool Lvmd::RemoveLogicalVolumes(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    const lvmd::RemoveLogicalVolumesRequest& in_request,
    lvmd::RemoveLogicalVolumesResponse* out_response) {
  for (const auto& lv : in_request.logical_volume_list().logical_volume()) {
    if (!IsClientAuthorized(error, message, lv.name())) {
      return false;
    }
  }

  out_response->clear_logical_volume_list();
  for (const auto& lv : in_request.logical_volume_list().logical_volume()) {
    brillo::ErrorPtr tmp_err;
    if (!RemoveLogicalVolume(&tmp_err, message, lv)) {
      // Only add failed to remove logical volumes into the response.
      // This will allow users to act on the logical volumes easier.
      auto* lv_added =
          out_response->mutable_logical_volume_list()->add_logical_volume();
      lv_added->CopyFrom(lv);
    }
  }

  if (out_response->has_logical_volume_list()) {
    std::vector<std::string> failed_lvs(
        out_response->logical_volume_list().logical_volume_size());
    for (const auto& lv :
         out_response->logical_volume_list().logical_volume()) {
      failed_lvs.push_back(lv.name());
    }
    *error = CreateError(
        FROM_HERE, kErrorInternal,
        base::StringPrintf("Failed to RemoveLogicalVolumes: %s",
                           base::JoinString(failed_lvs, ", ").c_str()));
    return false;
  }
  return true;
}

bool Lvmd::ToggleLogicalVolumeActivation(
    brillo::ErrorPtr* error,
    dbus::Message* message,
    const lvmd::LogicalVolume& in_logical_volume,
    bool activate) {
  std::string lv_name = in_logical_volume.name();
  if (!IsClientAuthorized(error, message, lv_name)) {
    return false;
  }

  auto vg_name = in_logical_volume.volume_group().name();
  auto vg = brillo::VolumeGroup(vg_name, {});

  auto opt_lv = lvm_->GetLogicalVolume(vg, lv_name);

  if (!opt_lv) {
    *error = CreateError(
        FROM_HERE, kErrorInternal,
        base::StringPrintf("Failed to GetLogicalVolume for lv (%s) in vg (%s)",
                           lv_name.c_str(), vg_name.c_str()));
    return false;
  }

  if (activate) {
    if (!opt_lv->Activate()) {
      *error =
          CreateError(FROM_HERE, kErrorInternal,
                      base::StringPrintf("Failed to activate for lv "
                                         "name (%s) in vg (%s)",
                                         lv_name.c_str(), vg_name.c_str()));
      return false;
    }
  } else if (!opt_lv->Deactivate()) {
    *error = CreateError(FROM_HERE, kErrorInternal,
                         base::StringPrintf("Failed to deactivate for lv "
                                            "name (%s) in vg (%s)",
                                            lv_name.c_str(), vg_name.c_str()));
    return false;
  }

  return true;
}

bool Lvmd::ResizeLogicalVolume(brillo::ErrorPtr* error,
                               dbus::Message* message,
                               const lvmd::LogicalVolume& in_logical_volume,
                               int64_t size) {
  std::string lv_name = in_logical_volume.name();
  if (!IsClientAuthorized(error, message, lv_name)) {
    return false;
  }

  auto vg_name = in_logical_volume.volume_group().name();
  auto vg = brillo::VolumeGroup(vg_name, {});

  auto opt_lv = lvm_->GetLogicalVolume(vg, lv_name);

  if (!opt_lv) {
    *error = CreateError(
        FROM_HERE, kErrorInternal,
        base::StringPrintf("Failed to GetLogicalVolume for lv (%s) in vg (%s)",
                           lv_name.c_str(), vg_name.c_str()));
    return false;
  }

  auto current_size = opt_lv->GetSize();
  auto blk_size = opt_lv->GetBlockSize();
  if (!current_size || !blk_size) {
    LOG(WARNING) << "Unable to check existing size, resizing regardless.";
  } else {
    // No need to resize if the size difference is less than one block since the
    // LVM tool automatically rounds up to the same size.
    if (auto size_delta = *current_size - size;
        size_delta >= 0 && size_delta < *blk_size) {
      LOG(WARNING) << "The size would not change, skip resizing.";
      return true;
    }
  }
  if (!opt_lv->Resize(size)) {
    *error = CreateError(FROM_HERE, kErrorInternal,
                         base::StringPrintf("Failed to resize lv "
                                            "name (%s) in vg (%s)",
                                            lv_name.c_str(), vg_name.c_str()));
    return false;
  }
  return true;
}

int Lvmd::OnInit() {
  int return_code = brillo::DBusServiceDaemon::OnInit();
  if (return_code != EX_OK) {
    return return_code;
  }

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
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, shutdown_callback_.callback(), kShutdownTimeout);
}

bool Lvmd::GetConnectionUnixUser(const std::string& connection_name,
                                 uid_t* out_uid) {
  dbus::ObjectProxy* dbus_proxy = bus_->GetObjectProxy(
      "org.freedesktop.DBus", dbus::ObjectPath("/org/freedesktop/DBus"));
  dbus::MethodCall method_call("org.freedesktop.DBus",
                               "GetConnectionUnixUser");
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(connection_name);

  auto response_expected = dbus_proxy->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!response_expected.has_value()) {
    LOG(ERROR) << "Failed to call GetConnectionUnixUser";
    return false;
  }
  std::unique_ptr<dbus::Response> response =
      std::move(response_expected.value());
  dbus::MessageReader reader(response.get());
  uint32_t uid;
  if (!reader.PopUint32(&uid)) {
    LOG(ERROR) << "Failed to read UID from GetConnectionUnixUser response";
    return false;
  }
  *out_uid = uid;
  return true;
}

bool Lvmd::IsClientAuthorized(brillo::ErrorPtr* error,
                             dbus::Message* message,
                             const std::string& lv_name) {
  if (!message) {
    // Allows calls from tests where message is not provided.
    return true;
  }

  std::string sender = message->GetSender();
  if (sender.empty()) {
    *error = CreateError(FROM_HERE, kErrorInternal, "Sender is empty");
    return false;
  }

  uid_t caller_uid;
  if (!GetConnectionUnixUser(sender, &caller_uid)) {
    *error = CreateError(FROM_HERE, kErrorInternal, "Failed to get sender UID");
    return false;
  }

  if (caller_uid == 0) {
    // Root is always allowed
    return true;
  }

  uid_t dlcservice_uid;
  if (!brillo::userdb::GetUserInfo("dlcservice", &dlcservice_uid, nullptr)) {
    *error = CreateError(FROM_HERE, kErrorInternal,
                         "Failed to get dlcservice UID");
    return false;
  }

  if (caller_uid == dlcservice_uid) {
    if (base::StartsWith(lv_name, "dlc_", base::CompareCase::SENSITIVE)) {
      return true;
    }
    *error = CreateError(
        FROM_HERE, kErrorInternal,
        "dlcservice is only allowed to access logical volumes starting with "
        "'dlc_'");
    return false;
  }

  *error = CreateError(FROM_HERE, kErrorInternal, "Unauthorized caller");
  return false;
}

}  // namespace lvmd
