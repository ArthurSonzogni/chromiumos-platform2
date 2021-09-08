// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>
#include <unistd.h>

#include <base/check.h>
#include <base/check_op.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>

#include "fusebox/fuse_frontend.h"
#include "fusebox/util.h"

using base::CommandLine;

namespace {

void SetupLogging() {
  brillo::InitLog(brillo::kLogToStderr);
}

}  // namespace

namespace fusebox {

class FuseBoxDaemon : public brillo::DBusServiceDaemon {
 public:
  explicit FuseBoxDaemon(FuseMount* fuse)
      : DBusServiceDaemon(kFuseBoxClientName), fuse_(fuse) {}
  FuseBoxDaemon(const FuseBoxDaemon&) = delete;
  FuseBoxDaemon& operator=(const FuseBoxDaemon&) = delete;
  ~FuseBoxDaemon() {}

 protected:
  // brillo::DBusServiceDaemon overrides.

  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    bus_->AssertOnDBusThread();

    // TODO(noel): register the FuseBoxClient DBUS objects.
  }

  int OnEventLoopStarted() override {
    bus_->AssertOnDBusThread();

    int ret = brillo::DBusServiceDaemon::OnEventLoopStarted();
    if (ret != EX_OK)
      return ret;

    // TODO(noel): setup and start the FuseBoxClient here.
    CHECK(fuse_);
    return EX_OK;
  }

  void OnShutdown(int* exit_code) override {
    bus_->AssertOnDBusThread();

    DBusServiceDaemon::OnShutdown(exit_code);
  }

 private:
  // Fuse mount: not owned.
  FuseMount* fuse_ = nullptr;
};

int Run(char** mountpoint, fuse_chan* chan, fuse_args* args) {
  LOG(INFO) << base::StringPrintf("fusebox %s [%d]", *mountpoint, getpid());
  FuseMount fuse = FuseMount(mountpoint, chan, args);
  auto daemon = FuseBoxDaemon(&fuse);
  return daemon.Run();
}

}  // namespace fusebox

int main(int argc, char** argv) {
  CommandLine::Init(argc, argv);
  SetupLogging();

  fuse_args args = FUSE_ARGS_INIT(argc, argv);
  char* mountpoint = nullptr;

  if (fuse_parse_cmdline(&args, &mountpoint, nullptr, nullptr) == -1) {
    LOG(ERROR) << "fuse_parse_cmdline() failed " << ErrorToString(errno);
    return EX_USAGE;
  }

  if (!mountpoint) {
    LOG(ERROR) << "fuse_parse_cmdline() mountpoint expected";
    return ENODEV;
  }

  fuse_chan* chan = fuse_mount(mountpoint, &args);
  if (!chan) {
    LOG(ERROR) << "fuse_mount() failed " << ErrorToString(errno, mountpoint);
    return ENODEV;
  }

  int exit_code = fusebox::Run(&mountpoint, chan, &args);

  if (!mountpoint) {  // Kernel can remove the FUSE mountpoint: umount(8).
    exit_code = ENODEV;
  } else {
    fuse_unmount(mountpoint, nullptr);
  }

  LOG_IF(ERROR, exit_code) << ErrorToString(exit_code, "exiting");
  fuse_opt_free_args(&args);

  return exit_code;
}
