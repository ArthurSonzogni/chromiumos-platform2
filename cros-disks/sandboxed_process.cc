// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/sandboxed_process.h"

#include <base/logging.h>
#include <chromeos/libminijail.h>

using std::string;

namespace cros_disks {

SandboxedProcess::SandboxedProcess()
    : jail_(minijail_new()) {
  CHECK(jail_) << "Failed to create a process jail";
}

SandboxedProcess::~SandboxedProcess() {
  minijail_destroy(jail_);
}

void SandboxedProcess::LoadSeccompFilterPolicy(const string& policy_file) {
  minijail_parse_seccomp_filters(jail_, policy_file.c_str());
  minijail_use_seccomp_filter(jail_);
}

void SandboxedProcess::NewMountNamespace() {
  minijail_namespace_vfs(jail_);
}

void SandboxedProcess::SkipRemountPrivate() {
  minijail_skip_remount_private(jail_);
}

void SandboxedProcess::SetCapabilities(uint64_t capabilities) {
  minijail_use_caps(jail_, capabilities);
}

void SandboxedProcess::SetGroupId(gid_t group_id) {
  minijail_change_gid(jail_, group_id);
}

void SandboxedProcess::SetUserId(uid_t user_id) {
  minijail_change_uid(jail_, user_id);
}

bool SandboxedProcess::Start() {
  char** arguments = GetArguments();
  CHECK(arguments) << "No argument is provided.";

  pid_t child_pid = kInvalidProcessId;
  int result = minijail_run_pid(jail_, arguments[0], arguments, &child_pid);
  if (result == 0) {
    set_pid(child_pid);
    return true;
  }

  return false;
}

int SandboxedProcess::Wait() {
  return minijail_wait(jail_);
}

int SandboxedProcess::Run() {
  if (Start()) {
    return Wait();
  }
  return -1;
}

}  // namespace cros_disks
