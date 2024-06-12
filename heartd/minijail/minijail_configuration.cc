// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/minijail/minijail_configuration.h"

#include <libminijail.h>
#include <scoped_minijail.h>

#include <base/check_op.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>

namespace heartd {

namespace {

constexpr char kHeartdUser[] = "heartd";
constexpr char kHeartdGroup[] = "heartd";
constexpr char kHeartdSeccompPath[] = "/usr/share/policy/heartd-seccomp.policy";

}  // namespace

void EnterHeartdMinijail() {
  ScopedMinijail j(minijail_new());
  minijail_no_new_privs(j.get());
  minijail_remount_proc_readonly(j.get());
  minijail_namespace_ipc(j.get());
  minijail_namespace_net(j.get());
  minijail_namespace_uts(j.get());
  minijail_namespace_vfs(j.get());
  minijail_enter_pivot_root(j.get(), "/mnt/empty");

  minijail_bind(j.get(), "/", "/", 0);
  minijail_bind(j.get(), "/proc", "/proc", 0);
  minijail_bind(j.get(), "/dev", "/dev", 0);

  // Create a new tmpfs filesystem for /run and mount necessary files.
  minijail_mount_with_data(j.get(), "tmpfs", "/run", "tmpfs", 0, "");
  // The socket file for the mojo service manager.
  minijail_bind(j.get(), "/run/mojo", "/run/mojo", 0);
  // Shared socket file for talking to the D-Bus daemon.
  minijail_bind(j.get(), "/run/dbus", "/run/dbus", 0);

  // Create a new tmpfs filesystem for /var and mount necessary files.
  minijail_mount_with_data(j.get(), "tmpfs", "/var", "tmpfs", 0, "");
  // For database.
  minijail_bind(j.get(), "/var/lib/heartd", "/var/lib/heartd", 1);
  // Symlink for reading the previous shutdown metrics.
  if (base::PathExists(base::FilePath("/var/log/metrics"))) {
    minijail_bind(j.get(), "/var/log/metrics", "/var/log/metrics", 0);
  }
  // Boot id information.
  if (base::PathExists(base::FilePath("/var/log/boot_id.log"))) {
    minijail_bind(j.get(), "/var/log/boot_id.log", "/var/log/boot_id.log", 0);
  }

  // Create a new tmpfs filesystem for /sys and mount necessary files.
  minijail_mount_with_data(j.get(), "tmpfs", "/sys", "tmpfs", 0, "");
  minijail_bind(j.get(), "/sys/devices", "/sys/devices", 0);
  if (base::PathExists(base::FilePath("/sys/class/intel_pmt"))) {
    minijail_bind(j.get(), "/sys/class/intel_pmt", "/sys/class/intel_pmt", 0);
  }

  CHECK_EQ(0, minijail_change_user(j.get(), kHeartdUser));
  CHECK_EQ(0, minijail_change_group(j.get(), kHeartdGroup));
  minijail_inherit_usergroups(j.get());

  minijail_use_seccomp_filter(j.get());
  minijail_parse_seccomp_filters(j.get(), kHeartdSeccompPath);

  minijail_enter(j.get());
}

}  // namespace heartd
