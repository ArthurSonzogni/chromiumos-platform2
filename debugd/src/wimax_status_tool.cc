// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/wimax_status_tool.h"

#include <base/logging.h>

#include "debugd/src/process_with_output.h"

using base::StringPrintf;

namespace debugd {

std::string WiMaxStatusTool::GetWiMaxStatus() {
  if (!USE_WIMAX)
    return "";

  std::string path;
  if (!SandboxedProcess::GetHelperPath("wimax_status", &path))
    return "";

  ProcessWithOutput p;
  p.Init();
  p.AddArg(path);
  p.Run();
  std::string out;
  p.GetOutput(&out);
  return out;
}

}  // namespace debugd
