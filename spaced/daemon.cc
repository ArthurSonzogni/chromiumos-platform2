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
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/spaced/dbus-constants.h>

#include "spaced/disk_usage.h"

namespace spaced {
DBusAdaptor::DBusAdaptor(scoped_refptr<dbus::Bus> bus)
    : org::chromium::SpacedAdaptor(this),
      dbus_object_(
          nullptr, bus, dbus::ObjectPath(::spaced::kSpacedServicePath)),
      disk_usage_util_(std::make_unique<DiskUsageUtil>()) {}

void DBusAdaptor::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(cb);
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
