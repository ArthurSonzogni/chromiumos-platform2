// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/crosh_shell_tool.h"

#include <base/files/file_util.h>

#include "debugd/src/error_utils.h"

namespace debugd {

namespace {

constexpr char kShShell[] = "/bin/sh";
constexpr char kBashShell[] = "/bin/bash";
constexpr char kChronosUser[] = "chronos";

const char kCroshToolErrorString[] = "org.chromium.debugd.error.CroshShell";

bool BashShellAvailable() {
  return base::PathExists(base::FilePath(kBashShell));
}

bool PreExecSetup(int fd) {
  // Dup the FD, because otherwise ScopedFD will close the only copy of this
  // pipe and the read end will give an error before the shell has exited.
  return HANDLE_EINTR(dup(fd)) != 0;
}

}  // namespace

bool CroshShellTool::Run(const base::ScopedFD& shell_lifeline_fd,
                         const base::ScopedFD& infd,
                         const base::ScopedFD& outfd,
                         std::string* out_id,
                         brillo::ErrorPtr* error) {
  // Sandbox options are similar to those to launch Chrome from the
  // login_manager service, but new_privs are allowed.
  ProcessWithId* p = CreateProcess(
      false /* Skip default Minijail args. */, true /* access_root_mount_ns. */,
      {"-u", kChronosUser, "-g", kChronosUser, "-G" /* Inherit groups. */,
       "--no-default-runtime-environment"});
  if (!p) {
    DEBUGD_ADD_ERROR(error, kCroshToolErrorString,
                     "Could not create crosh shell process");
    return false;
  }
  p->AddArg(BashShellAvailable() ? kBashShell : kShShell);
  p->BindFd(shell_lifeline_fd.get(), shell_lifeline_fd.get());
  p->SetPreExecCallback(base::BindOnce(&PreExecSetup, shell_lifeline_fd.get()));
  p->BindFd(infd.get(), STDIN_FILENO);
  p->BindFd(outfd.get(), STDOUT_FILENO);
  p->BindFd(outfd.get(), STDERR_FILENO);
  p->Start();
  *out_id = p->id();
  return true;
}

}  // namespace debugd
