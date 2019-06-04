// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/subprocess.h"

#include <algorithm>
#include <memory>
#include <vector>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <base/logging.h>
#include <base/posix/file_descriptor_shuffle.h>
#include <base/process/launch.h>
#include <base/time/time.h>

#include <libminijail.h>
#include <scoped_minijail.h>

#include "login_manager/session_manager_service.h"
#include "login_manager/system_utils.h"

namespace login_manager {

Subprocess::Subprocess(uid_t uid, SystemUtils* system)
    : pid_(-1),
      desired_uid_(uid),
      new_mount_namespace_(false),
      system_(system) {}

Subprocess::~Subprocess() {}

void Subprocess::UseNewMountNamespace() {
  new_mount_namespace_ = true;
}

bool Subprocess::ForkAndExec(const std::vector<std::string>& args,
                             const std::vector<std::string>& env_vars) {
  gid_t gid = 0;
  std::vector<gid_t> groups;
  if (desired_uid_ != 0 &&
      !system_->GetGidAndGroups(desired_uid_, &gid, &groups)) {
    LOG(ERROR) << "Can't get group info for UID " << desired_uid_;
    return false;
  }

  ScopedMinijail j(minijail_new());
  if (desired_uid_ != 0) {
    minijail_change_uid(j.get(), desired_uid_);
    minijail_change_gid(j.get(), gid);
    minijail_set_supplementary_gids(j.get(), groups.size(), groups.data());
  }
  minijail_preserve_fd(j.get(), STDIN_FILENO, STDIN_FILENO);
  minijail_preserve_fd(j.get(), STDOUT_FILENO, STDOUT_FILENO);
  minijail_preserve_fd(j.get(), STDERR_FILENO, STDERR_FILENO);
  minijail_close_open_fds(j.get());
  // Reset signal handlers in the child since they'll be blocked below.
  minijail_reset_signal_mask(j.get());
  minijail_reset_signal_handlers(j.get());

  if (new_mount_namespace_) {
    minijail_namespace_vfs(j.get());
  }

  // Block all signals before running the child so that we can avoid a race
  // in which the child executes configured signal handlers before the default
  // handlers are installed. In the parent, we restore original signal blocks
  // immediately after SystemUtils::RunInMinijail().
  sigset_t filled_sigset, old_sigset;
  sigfillset(&filled_sigset);
  CHECK_EQ(0, sigprocmask(SIG_SETMASK, &filled_sigset, &old_sigset));

  pid_t child_pid = 0;
  bool success = system_->RunInMinijail(j, args, env_vars, &child_pid);

  CHECK_EQ(0, sigprocmask(SIG_SETMASK, &old_sigset, nullptr));

  if (success) {
    pid_ = child_pid;
  }

  return pid_.has_value();
}

void Subprocess::KillEverything(int signal) {
  DCHECK(pid_.has_value());
  if (system_->kill(-pid_.value(), desired_uid_, signal) == 0)
    return;

  // If we failed to kill the process group (maybe it doesn't exist yet because
  // the forked process hasn't had a chance to call setsid()), just kill the
  // child directly. If it hasn't called setsid() yet, then it hasn't called
  // setuid() either, so kill it as root instead of as |desired_uid_|.
  system_->kill(pid_.value(), 0, signal);
}

void Subprocess::Kill(int signal) {
  DCHECK(pid_.has_value());
  system_->kill(pid_.value(), desired_uid_, signal);
}

pid_t Subprocess::GetPid() const {
  return pid_.value_or(-1);
}

void Subprocess::ClearPid() {
  pid_.reset();
}

};  // namespace login_manager
