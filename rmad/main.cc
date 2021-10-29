// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/capability.h>
#include <sys/mount.h>

#include <base/logging.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <libminijail.h>
#include <scoped_minijail.h>

#include "rmad/dbus_service.h"
#include "rmad/rmad_interface_impl.h"
#include "rmad/utils/crossystem_utils_impl.h"

namespace {

constexpr char kRmadUser[] = "rmad";
constexpr char kRmadGroup[] = "rmad";
constexpr char kSeccompFilterPath[] = "/usr/share/policy/rmad-seccomp.policy";

}  // namespace

void EnterMinijail() {
  ScopedMinijail j(minijail_new());
  minijail_no_new_privs(j.get());
  minijail_remount_proc_readonly(j.get());
  minijail_namespace_ipc(j.get());
  minijail_namespace_net(j.get());
  minijail_namespace_uts(j.get());
  minijail_namespace_vfs(j.get());

  minijail_change_user(j.get(), kRmadUser);
  minijail_change_group(j.get(), kRmadGroup);
  minijail_inherit_usergroups(j.get());

  minijail_enter_pivot_root(j.get(), "/mnt/empty");

  minijail_mount_tmp(j.get());
  minijail_bind(j.get(), "/", "/", 0);
  minijail_bind(j.get(), "/dev/", "/dev", 0);
  minijail_bind(j.get(), "/proc", "/proc", 0);

  minijail_mount_with_data(j.get(), "tmpfs", "/run", "tmpfs", 0, nullptr);
  minijail_bind(j.get(), "/run/chromeos-config/v1", "/run/chromeos-config/v1",
                0);
  minijail_bind(j.get(), "/run/dbus", "/run/dbus", 0);
  minijail_bind(j.get(), "/run/lock", "/run/lock", 1);

  minijail_mount_with_data(j.get(), "tmpfs", "/var", "tmpfs", 0, nullptr);
  minijail_bind(j.get(), "/var/lib/devicesettings", "/var/lib/devicesettings",
                0);
  minijail_bind(j.get(), "/var/lib/rmad", "/var/lib/rmad", 1);
  minijail_bind(j.get(), "/var/log", "/var/log", 0);

  minijail_mount_with_data(j.get(), "tmpfs", "/sys", "tmpfs", 0, nullptr);
  minijail_bind(j.get(), "/sys/devices", "/sys/devices", 1);
  minijail_bind(j.get(), "/sys/class", "/sys/class", 0);
  minijail_bind(j.get(), "/sys/bus", "/sys/bus", 0);

  minijail_mount_with_data(j.get(), "tmpfs", "/mnt/stateful_partition", "tmpfs",
                           0, nullptr);
  minijail_bind(j.get(), "/mnt/stateful_partition/unencrypted/rma-data",
                "/mnt/stateful_partition/unencrypted/rma-data", 1);

  rmad::CrosSystemUtilsImpl crossystem_utils;
  int wpsw_cur;
  if (crossystem_utils.GetInt("wpsw_cur", &wpsw_cur) && wpsw_cur == 0) {
    VLOG(1) << "Hardware write protection off.";
    minijail_use_caps(j.get(), CAP_TO_MASK(CAP_SYS_RAWIO) |
                                   CAP_TO_MASK(CAP_DAC_OVERRIDE) |
                                   CAP_TO_MASK(CAP_SYS_ADMIN));
    minijail_set_ambient_caps(j.get());
    minijail_bind(j.get(), "/dev/mem", "/dev/mem", 0);
  } else {
    VLOG(1) << "Hardware write protection on.";
  }

  minijail_use_seccomp_filter(j.get());
  minijail_parse_seccomp_filters(j.get(), kSeccompFilterPath);

  minijail_enter(j.get());
}

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  DEFINE_bool(test_mode, false, "Run in the mode to use fake state handlers");
  brillo::FlagHelper::Init(argc, argv, "Chrome OS RMA Daemon");

  VLOG(1) << "Starting Chrome OS RMA Daemon.";

  EnterMinijail();

  base::ThreadPoolInstance::CreateAndStartWithDefaultParams("rmad_thread_pool");

  rmad::RmadInterfaceImpl rmad_interface;
  rmad::DBusService dbus_service(&rmad_interface);
  if (FLAGS_test_mode) {
    LOG(INFO) << "Running in test mode";
    dbus_service.SetTestMode();
    rmad_interface.SetTestMode();
  }

  return dbus_service.Run();
}
