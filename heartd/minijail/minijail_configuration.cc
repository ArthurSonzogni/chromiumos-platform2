// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "heartd/minijail/minijail_configuration.h"

#include <base/check_op.h>
#include <libminijail.h>
#include <scoped_minijail.h>

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

  CHECK_EQ(0, minijail_change_user(j.get(), kHeartdUser));
  CHECK_EQ(0, minijail_change_group(j.get(), kHeartdGroup));
  minijail_inherit_usergroups(j.get());

  minijail_use_seccomp_filter(j.get());
  minijail_parse_seccomp_filters(j.get(), kHeartdSeccompPath);

  minijail_enter(j.get());
}

}  // namespace heartd
