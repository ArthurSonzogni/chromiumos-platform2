// Copyright 2023 The ChromiumOS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/crosh_tool.h"

#include "debugd/src/error_utils.h"

namespace debugd {

namespace {

constexpr char kDefaultShell[] = "/usr/bin/crosh";
const char kCroshToolErrorString[] = "org.chromium.debugd.error.Crosh";

}  // namespace

bool CroshTool::Run(const base::ScopedFD& infd,
                    const base::ScopedFD& outfd,
                    std::string* out_id,
                    brillo::ErrorPtr* error) {
  // Sandbox options are similar to those to launch Chrome from the
  // login_manager service, but new_privs are allowed.
  // TODO(b/309243217): apply Landlock policy.
  ProcessWithId* p = CreateProcess(true /* sandboxed */,
                                   false /* access_root_mount_ns */, {"-pvr"});
  if (!p) {
    DEBUGD_ADD_ERROR(error, kCroshToolErrorString,
                     "Could not create crosh process");
    return false;
  }

  p->AddArg(kDefaultShell);
  p->BindFd(infd.get(), STDIN_FILENO);
  p->BindFd(outfd.get(), STDOUT_FILENO);
  p->BindFd(outfd.get(), STDERR_FILENO);
  p->Start();
  *out_id = p->id();
  return true;
}

}  // namespace debugd
