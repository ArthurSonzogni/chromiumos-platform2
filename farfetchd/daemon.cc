// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "farfetchd/daemon.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sysexits.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>

#include "farfetchd/prefetch_helper.h"
#include "libstorage/platform/platform.h"

namespace farfetchd {

namespace {

constexpr char kFarfetchdServiceName[] = "org.chromium.Farfetchd";
constexpr char kFarfetchdServicePath[] = "/org/chromium/Farfetchd";

}  // namespace

DBusAdaptor::DBusAdaptor(scoped_refptr<dbus::Bus> bus)
    : org::chromium::FarfetchdAdaptor(this),
      helper_(std::make_unique<PrefetchHelper>(&platform_)),
      dbus_object_(
          nullptr, bus, dbus::ObjectPath(::farfetchd::kFarfetchdServicePath)) {}

void DBusAdaptor::RegisterAsync(
    brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(std::move(cb));
}

// Preload file by reading it into memory
bool DBusAdaptor::PreloadFile(const std::string& p) {
  return helper_->PreloadFile(base::FilePath(p));
}

// Preload file by reading it into memory asynchronously.
// Scheduling is handled by the kernel so the actual caching
// may be delayed.
bool DBusAdaptor::PreloadFileAsync(const std::string& p) {
  return helper_->PreloadFileAsync(base::FilePath(p));
}

// Preload file by mmapping it into memory
bool DBusAdaptor::PreloadFileMmap(const std::string& p) {
  return helper_->PreloadFileMmap(base::FilePath(p));
}

Daemon::Daemon() : DBusServiceDaemon(kFarfetchdServiceName) {}

void Daemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  adaptor_ = std::make_unique<DBusAdaptor>(bus_);
  adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
}

}  // namespace farfetchd
