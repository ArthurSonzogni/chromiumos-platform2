// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/tracepath_tool.h"

#include "debugd/src/process_with_id.h"

#include <base/logging.h>

namespace debugd {

namespace {

const char kTracepath[] = "/usr/sbin/tracepath";
const char kTracepath6[] = "/usr/sbin/tracepath6";

}  // namespace

std::string TracePathTool::Start(const base::ScopedFD& outfd,
                                 const std::string& destination,
                                 const brillo::VariantDictionary& options) {
  ProcessWithId* p =
      CreateProcess(true /* sandboxed */, false /* access_root_mount_ns */);
  if (!p)
    return "";

  if (brillo::GetVariantValueOrDefault<bool>(options, "v6"))
    p->AddArg(kTracepath6);
  else
    p->AddArg(kTracepath);

  if (options.count("numeric") == 1)
    p->AddArg("-n");

  p->AddArg(destination);
  p->BindFd(outfd.get(), STDOUT_FILENO);
  p->BindFd(outfd.get(), STDERR_FILENO);
  LOG(INFO) << "tracepath: running process id: " << p->id();
  p->Start();
  return p->id();
}

}  // namespace debugd
