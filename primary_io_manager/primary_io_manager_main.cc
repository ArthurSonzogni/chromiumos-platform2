// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/command_line.h>
#include <base/time/time.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>

#include "primary_io_manager/primary_io_manager.h"

namespace primary_io_manager {

using brillo::dbus_utils::AsyncEventSequencer;

class Daemon : public brillo::DBusServiceDaemon {
 public:
  Daemon() : DBusServiceDaemon(kPrimaryIoManagerServiceName) {}
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

 protected:
  void RegisterDBusObjectsAsync(AsyncEventSequencer* sequencer) override {
    manager_ = std::make_unique<PrimaryIoManager>(bus_);
    manager_->RegisterAsync(AsyncEventSequencer::GetDefaultCompletionAction());
  }

 private:
  std::unique_ptr<PrimaryIoManager> manager_;
  base::TimeDelta poll_interval_;
};

}  // namespace primary_io_manager

int main(int argc, char** argv) {
  brillo::FlagHelper::Init(argc, argv, "Chromium OS Primary IO Manager");
  brillo::InitLog(brillo::kLogToSyslog);

  primary_io_manager::Daemon daemon{};

  // TODO(b/308434587): only run on chromebox
  return daemon.Run();
}
