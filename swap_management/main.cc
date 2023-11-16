// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/dbus_adaptor.h"
#include "swap_management/metrics.h"

#include <sys/mount.h>
#include <sys/stat.h>
#include <sysexits.h>
#include <unistd.h>

#include <absl/status/status.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>
#include <memory>

namespace {

class Daemon : public brillo::DBusServiceDaemon {
 public:
  Daemon() : DBusServiceDaemon(swap_management::kSwapManagementServiceName) {}
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

 protected:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    adaptor_.reset(new swap_management::DBusAdaptor(bus_));
    adaptor_->RegisterAsync(
        sequencer->GetHandler("RegisterAsync() failed.", true));
  }

 private:
  std::unique_ptr<swap_management::DBusAdaptor> adaptor_;
};

}  // namespace

int main(int argc, char* argv[]) {
  DEFINE_bool(swap_stop, false, "Stop zram swap");
  brillo::FlagHelper::Init(argc, argv, "CrOS swap_management");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  if (FLAGS_swap_stop) {
    std::unique_ptr<swap_management::SwapTool> swap_tool =
        std::make_unique<swap_management::SwapTool>();

    absl::Status status = swap_tool->SwapStop();
    swap_management::Metrics::Get()->ReportSwapStopStatus(status);

    if (!status.ok()) {
      LOG(ERROR) << "Failed to stop swap: " << status;
      return EX_SOFTWARE;
    }

    return EX_OK;
  } else if (argc > 1) {
    LOG(ERROR) << "Unhandled arguments; please see --help for more info.";
    return EX_USAGE;
  }

  Daemon().Run();
  return EX_OK;
}
