// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spaced/daemon.h"

#include <sysexits.h>

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/spaced/dbus-constants.h>
#include <rootdev/rootdev.h>

#include "spaced/disk_usage_impl.h"

namespace spaced {
namespace {

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

  // For some storage devices (eg. eMMC), the path ends in a digit
  // (eg. /dev/mmcblk0). Use 'p' as the partition separator while generating
  // the partition's block device path. For other types of paths (/dev/sda), we
  // directly append the partition number.
  std::string stateful_dev(root_device.value());
  if (base::IsAsciiDigit(stateful_dev[stateful_dev.size() - 1]))
    stateful_dev += 'p';
  stateful_dev += '1';

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
      disk_usage_util_(std::make_unique<DiskUsageUtilImpl>(GetRootDevice(),
                                                           GetThinpool())) {}

void DBusAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

int64_t DBusAdaptor::GetFreeDiskSpace(const std::string& path) {
  return disk_usage_util_->GetFreeDiskSpace(base::FilePath(path));
}

int64_t DBusAdaptor::GetTotalDiskSpace(const std::string& path) {
  return disk_usage_util_->GetTotalDiskSpace(base::FilePath(path));
}

int64_t DBusAdaptor::GetRootDeviceSize() {
  return disk_usage_util_->GetRootDeviceSize();
}

Daemon::Daemon() : DBusServiceDaemon(::spaced::kSpacedServiceName) {}

void Daemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  adaptor_.reset(new DBusAdaptor(bus_));
  adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
}

}  // namespace spaced
