// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spaced/daemon.h"

#include <sysexits.h>

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/strings/string_util.h>
#include <base/task/task_runner.h>
#include <base/task/thread_pool.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <brillo/blkdev_utils/storage_utils.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/spaced/dbus-constants.h>
#include <rootdev/rootdev.h>
#include <spaced/proto_bindings/spaced.pb.h>

#include "spaced/disk_usage_impl.h"

namespace spaced {
namespace {
constexpr int64_t kCriticalRefreshPeriodSeconds = 1;

base::FilePath GetRootDevice() {
  // Get the root device.
  char root_device[PATH_MAX];
  int ret = rootdev(root_device, sizeof(root_device),
                    true,   // Do full resolution.
                    true);  // Remove partition number.
  if (ret != 0) {
    LOG(WARNING) << "rootdev failed with error code " << ret;
    return base::FilePath();
  }

  return base::FilePath(root_device);
}

std::optional<brillo::Thinpool> GetThinpool() {
  base::FilePath root_device = GetRootDevice();

  if (root_device.empty()) {
    LOG(WARNING) << "Failed to get root device";
    return std::nullopt;
  }

  const base::FilePath stateful_dev = brillo::AppendPartition(root_device, 1);

  // Attempt to check if the stateful partition is setup with a valid physical
  // volume.
  base::FilePath physical_volume(stateful_dev);

  brillo::LogicalVolumeManager lvm;
  std::optional<brillo::PhysicalVolume> pv =
      lvm.GetPhysicalVolume(physical_volume);
  if (!pv || !pv->IsValid())
    return std::nullopt;

  std::optional<brillo::VolumeGroup> vg = lvm.GetVolumeGroup(*pv);
  if (!vg || !vg->IsValid())
    return std::nullopt;

  return lvm.GetThinpool(*vg, "thinpool");
}

}  // namespace

DBusAdaptor::DBusAdaptor(scoped_refptr<dbus::Bus> bus)
    : org::chromium::SpacedAdaptor(this),
      dbus_object_(
          nullptr, bus, dbus::ObjectPath(::spaced::kSpacedServicePath)),
      disk_usage_util_(
          std::make_unique<DiskUsageUtilImpl>(GetRootDevice(), GetThinpool())),
      task_runner_(bus->GetOriginTaskRunner()),
      stateful_free_space_calculator_(
          std::make_unique<StatefulFreeSpaceCalculator>(
              task_runner_,
              kCriticalRefreshPeriodSeconds,
              GetThinpool(),
              base::BindRepeating(&DBusAdaptor::StatefulDiskSpaceUpdateCallback,
                                  base::Unretained(this)))) {
  stateful_free_space_calculator_->Start();
}

void DBusAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

int64_t DBusAdaptor::GetFreeDiskSpace(const std::string& path) {
  int64_t free_space = disk_usage_util_->GetFreeDiskSpace(base::FilePath(path));

  // Note that GetSize() occurs on the D-bus thread whereas the actual stateful
  // free space calculation will be asynchronously handled and updated.
  return std::min(free_space, stateful_free_space_calculator_->GetSize());
}
int64_t DBusAdaptor::GetTotalDiskSpace(const std::string& path) {
  return disk_usage_util_->GetTotalDiskSpace(base::FilePath(path));
}

int64_t DBusAdaptor::GetRootDeviceSize() {
  return disk_usage_util_->GetRootDeviceSize();
}

bool DBusAdaptor::IsQuotaSupported(const std::string& path) {
  return disk_usage_util_->IsQuotaSupported(base::FilePath(path));
}

int64_t DBusAdaptor::GetQuotaCurrentSpaceForUid(const std::string& path,
                                                uint32_t uid) {
  return disk_usage_util_->GetQuotaCurrentSpaceForUid(base::FilePath(path),
                                                      uid);
}

int64_t DBusAdaptor::GetQuotaCurrentSpaceForGid(const std::string& path,
                                                uint32_t gid) {
  return disk_usage_util_->GetQuotaCurrentSpaceForGid(base::FilePath(path),
                                                      gid);
}

int64_t DBusAdaptor::GetQuotaCurrentSpaceForProjectId(const std::string& path,
                                                      uint32_t project_id) {
  return disk_usage_util_->GetQuotaCurrentSpaceForProjectId(
      base::FilePath(path), project_id);
}

SetProjectIdReply DBusAdaptor::SetProjectId(const base::ScopedFD& fd,
                                            uint32_t project_id) {
  SetProjectIdReply reply;
  int error = 0;
  reply.set_success(disk_usage_util_->SetProjectId(fd, project_id, &error));
  reply.set_error(error);
  return reply;
}

SetProjectInheritanceFlagReply DBusAdaptor::SetProjectInheritanceFlag(
    const base::ScopedFD& fd, bool enable) {
  SetProjectInheritanceFlagReply reply;
  int error = 0;
  reply.set_success(
      disk_usage_util_->SetProjectInheritanceFlag(fd, enable, &error));
  reply.set_error(error);
  return reply;
}

void DBusAdaptor::StatefulDiskSpaceUpdateCallback(
    const StatefulDiskSpaceUpdate& state) {
  SendStatefulDiskSpaceUpdateSignal(state);
}

Daemon::Daemon() : DBusServiceDaemon(::spaced::kSpacedServiceName) {}

void Daemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  adaptor_.reset(new DBusAdaptor(bus_));
  adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
}

}  // namespace spaced
