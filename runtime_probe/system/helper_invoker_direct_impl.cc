// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/system/helper_invoker_direct_impl.h"

#include <string>

#include <base/command_line.h>
#include <base/logging.h>
#include <brillo/process/process.h>

#include "runtime_probe/utils/pipe_utils.h"

namespace runtime_probe {

static_assert(USE_FACTORY_RUNTIME_PROBE,
              "The compiler should never reach " __FILE__
              " while building a regular runtime_probe.");

bool RuntimeProbeHelperInvokerDirectImpl::Invoke(
    const std::string& probe_statement, std::string* result) {
  brillo::ProcessImpl helper_proc;
  helper_proc.AddArg(
      base::CommandLine::ForCurrentProcess()->GetProgram().value());
  helper_proc.AddArg("--helper");
  helper_proc.AddArg(probe_statement);
  helper_proc.RedirectInput("/dev/null");
  helper_proc.RedirectUsingPipe(STDOUT_FILENO, false);

  if (!helper_proc.Start()) {
    LOG(ERROR) << "Failed to start the helper process.";
    return false;
  }

  return ReadNonblockingPipeToString(helper_proc.GetPipe(STDOUT_FILENO),
                                     result);
}

}  // namespace runtime_probe
