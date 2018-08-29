// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include <base/command_line.h>
#include <base/logging.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/syslog_logging.h>
#include <chromeos/dbus/service_constants.h>
#include <chromeos/libminijail.h>
#include <chromeos/scoped_minijail.h>

#include "debugd/src/debugd_dbus_adaptor.h"

namespace {

// For TPM 1.2 only: Directory to mount for access to tcsd socket.
constexpr char kTcsdDir[] = "/run/tcsd";

// @brief Enter a VFS namespace.
//
// We don't want anyone other than our descendants to see our tmpfs.
void enter_vfs_namespace() {
  ScopedMinijail j(minijail_new());

  // Create a minimalistic mount namespace with just the bare minimum required.
  minijail_namespace_vfs(j.get());
  if (minijail_enter_pivot_root(j.get(), "/var/empty"))
    LOG(FATAL) << "minijail_enter_pivot_root() failed";
  if (minijail_bind(j.get(), "/", "/", 0))
    LOG(FATAL) << "minijail_bind(\"/\") failed";
  if (minijail_mount_with_data(j.get(), "none", "/proc", "proc",
                               MS_NOSUID | MS_NOEXEC | MS_NODEV, nullptr)) {
    LOG(FATAL) << "minijail_mount_with_data(\"/proc\") failed";
  }
  if (minijail_bind(j.get(), "/var", "/var", 1))
    LOG(FATAL) << "minijail_bind(\"/var\") failed";

  // Hack a path for vpd until it can migrate to /var.
  // https://crbug.com/876838
  if (minijail_mount_with_data(j.get(), "tmpfs", "/mnt", "tmpfs",
                               MS_NOSUID | MS_NOEXEC | MS_NODEV,
                               "mode=0755,size=10M")) {
    LOG(FATAL) << "minijail_mount_with_data(\"/mnt\") failed";
  }
  const char kVpdPath[] = "/mnt/stateful_partition/unencrypted/cache/vpd";
  if (minijail_bind(j.get(), kVpdPath, kVpdPath, 1))
    LOG(FATAL) << "minijail_bind(\"" << kVpdPath << "\") failed";

  // Mount /run/dbus to be able to communicate with D-Bus.
  if (minijail_mount_with_data(j.get(), "tmpfs", "/run", "tmpfs",
                               MS_NOSUID | MS_NOEXEC | MS_NODEV, nullptr)) {
    LOG(FATAL) << "minijail_mount_with_data(\"/run\") failed";
  }
  if (minijail_bind(j.get(), "/run/dbus", "/run/dbus", 0))
    LOG(FATAL) << "minijail_bind(\"/run/dbus\") failed";

  // Mount /tmp, /run/cups, and /run/ippusb to be able to communicate with CUPS.
  if (minijail_bind(j.get(), "/tmp", "/tmp", 1))
    LOG(FATAL) << "minijail_bind(\"/tmp\") failed";
  // In case we start before cups, make sure the path exists.
  mkdir("/run/cups", 0755);
  if (minijail_bind(j.get(), "/run/cups", "/run/cups", 0))
    LOG(FATAL) << "minijail_bind(\"/run/cups\") failed";
  // In case we start before upstart-socket-bridge, make sure the path exists.
  mkdir("/run/ippusb", 0755);
  // Mount /run/ippusb to be able to communicate with CUPS.
  if (minijail_bind(j.get(), "/run/ippusb", "/run/ippusb", 0))
    LOG(FATAL) << "minijail_bind(\"/run/ippusb\") failed";

  // Since shill provides network resolution settings, bind mount it.
  // In case we start before shill, make sure the path exists.
  mkdir("/run/shill", 0755);
  if (minijail_bind(j.get(), "/run/shill", "/run/shill", 0))
    LOG(FATAL) << "minijail_bind(\"/run/shill\") failed";

  // Mount /run/arc/bugreport to be able to collect ARC bug reports.
  // In case we start before ARC, make sure the path exists.
  mkdir("/run/arc", 0755);
  mkdir("/run/arc/bugreport", 0755);
  if (minijail_bind(j.get(), "/run/arc/bugreport", "/run/arc/bugreport", 0))
    LOG(FATAL) << "minijail_bind(\"/run/arc/bugreport\") failed";

  // Mount /run/containers to be able to collect container stats.
  mkdir("/run/containers", 0755);
  if (minijail_bind(j.get(), "/run/containers", "/run/containers", 0))
    LOG(FATAL) << "minijail_bind(\"/run/containers\") failed";

  // Mount /dev to be able to inspect devices.
  if (minijail_mount_with_data(j.get(), "/dev", "/dev", "bind",
                               MS_BIND | MS_REC, nullptr)) {
    LOG(FATAL) << "minijail_mount_with_data(\"/dev\") failed";
  }

  // Mount /sys to access some logs.
  if (minijail_mount_with_data(j.get(), "/sys", "/sys", "bind",
                               MS_BIND | MS_REC, nullptr)) {
    LOG(FATAL) << "minijail_mount_with_data(\"/sys\") failed";
  }

  if (USE_TPM) {
    // For TPM 1.2 only: Enable utilities that communicate with TPM via tcsd -
    // mount directory containing tcsd socket.
    mkdir(kTcsdDir, 0755);
    if (minijail_bind(j.get(), kTcsdDir, kTcsdDir, 0)) {
      LOG(FATAL) << "minijail_bind(\"" << kTcsdDir << "\") failed";
    }
  }

  minijail_enter(j.get());
}

class Daemon : public brillo::DBusServiceDaemon {
 public:
  Daemon() : DBusServiceDaemon(debugd::kDebugdServiceName) {}

 protected:
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    adaptor_.reset(new debugd::DebugdDBusAdaptor(bus_));
    adaptor_->RegisterAsync(sequencer->GetHandler(
        "RegisterAsync() failed.", true));
  }

 private:
  std::unique_ptr<debugd::DebugdDBusAdaptor> adaptor_;

  DISALLOW_COPY_AND_ASSIGN(Daemon);
};

}  // namespace

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  enter_vfs_namespace();
  Daemon().Run();
  return 0;
}
