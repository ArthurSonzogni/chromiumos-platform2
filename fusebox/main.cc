// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>
#include <unistd.h>

#include <base/check.h>
#include <base/check_op.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <base/strings/stringprintf.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>

#include "fusebox/file_system.h"
#include "fusebox/file_system_fake.h"
#include "fusebox/fuse_frontend.h"
#include "fusebox/util.h"

using base::CommandLine;

namespace {

void SetupLogging() {
  brillo::InitLog(brillo::kLogToStderr);
}

static bool g_use_fake_file_system;

fusebox::FileSystem* CreateFakeFileSystem() {
  static base::NoDestructor<fusebox::FileSystemFake> fake_file_system;
  return fake_file_system.get();
}

}  // namespace

namespace fusebox {

class FuseBoxClient : public FileSystem {
 public:
  FuseBoxClient(scoped_refptr<dbus::Bus> bus, FuseMount* fuse)
      : fuse_(fuse), bus_(bus) {}
  FuseBoxClient(const FuseBoxClient&) = delete;
  FuseBoxClient& operator=(const FuseBoxClient&) = delete;
  virtual ~FuseBoxClient() = default;

  void RegisterDBusObjectsAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
    // TODO(noel): register FuseBoxClient DBUS objects.
  }

  int StartFuseSession(base::OnceClosure quit_callback) {
    quit_callback_ = std::move(quit_callback);

    fuse_frontend_.reset(new FuseFrontend(fuse_));
    auto* fs = g_use_fake_file_system ? CreateFakeFileSystem() : this;
    if (!fuse_frontend_->CreateFuseSession(fs, FileSystem::FuseOps()))
      return EX_SOFTWARE;

    auto stop = base::BindOnce(&FuseBoxClient::Stop, base::Unretained(this));
    fuse_frontend_->StartFuseSession(std::move(stop));
    return EX_OK;
  }

  void Stop() {
    fuse_frontend_.reset();
    if (quit_callback_) {
      std::move(quit_callback_).Run();
    }
  }

 private:
  // Fuse mount: not owned.
  FuseMount* fuse_ = nullptr;

  // D-Bus.
  scoped_refptr<dbus::Bus> bus_;

  // Fuse user-space frontend.
  std::unique_ptr<FuseFrontend> fuse_frontend_;

  // Quit callback.
  base::OnceClosure quit_callback_;
};

class FuseBoxDaemon : public brillo::DBusServiceDaemon {
 public:
  explicit FuseBoxDaemon(FuseMount* fuse)
      : DBusServiceDaemon(kFuseBoxClientName), fuse_(fuse) {}
  FuseBoxDaemon(const FuseBoxDaemon&) = delete;
  FuseBoxDaemon& operator=(const FuseBoxDaemon&) = delete;
  ~FuseBoxDaemon() = default;

 protected:
  // brillo::DBusServiceDaemon overrides.

  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    bus_->AssertOnDBusThread();

    client_.reset(new FuseBoxClient(bus_, fuse_));
    client_->RegisterDBusObjectsAsync(
        sequencer->GetHandler("D-Bus register async failed", true));
  }

  int OnEventLoopStarted() override {
    bus_->AssertOnDBusThread();

    int ret = brillo::DBusServiceDaemon::OnEventLoopStarted();
    if (ret != EX_OK)
      return ret;

    auto quit = base::BindOnce(&Daemon::Quit, base::Unretained(this));
    return client_->StartFuseSession(std::move(quit));
  }

  void OnShutdown(int* exit_code) override {
    bus_->AssertOnDBusThread();

    DBusServiceDaemon::OnShutdown(exit_code);
    client_.reset();
  }

 private:
  // Fuse mount: not owned.
  FuseMount* fuse_ = nullptr;

  // Fuse user-space client.
  std::unique_ptr<FuseBoxClient> client_;
};

int Run(char** mountpoint, fuse_chan* chan) {
  LOG(INFO) << base::StringPrintf("fusebox %s [%d]", *mountpoint, getpid());
  FuseMount fuse = FuseMount(mountpoint, chan);
  auto daemon = FuseBoxDaemon(&fuse);
  return daemon.Run();
}

}  // namespace fusebox

int main(int argc, char** argv) {
  CommandLine::Init(argc, argv);
  SetupLogging();

  if (CommandLine::ForCurrentProcess()->HasSwitch("fake"))
    g_use_fake_file_system = true;

  fuse_args args = FUSE_ARGS_INIT(argc, argv);
  char* mountpoint = nullptr;

  if (fuse_parse_cmdline(&args, &mountpoint, nullptr, nullptr) == -1) {
    PLOG(ERROR) << "fuse_parse_cmdline() failed";
    return EX_USAGE;
  }

  if (!mountpoint) {
    LOG(ERROR) << "fuse_parse_cmdline() mountpoint expected";
    return ENODEV;
  }

  fuse_chan* chan = fuse_mount(mountpoint, &args);
  if (!chan) {
    PLOG(ERROR) << "fuse_mount() [" << mountpoint << "] failed";
    return ENODEV;
  }

  int exit_code = fusebox::Run(&mountpoint, chan);

  if (!mountpoint) {  // Kernel removed the FUSE mountpoint: umount(8).
    exit_code = ENODEV;
  } else {
    fuse_unmount(mountpoint, nullptr);
  }

  errno = exit_code;
  LOG_IF(ERROR, exit_code) << "fusebox exiting";
  fuse_opt_free_args(&args);

  return exit_code;
}
