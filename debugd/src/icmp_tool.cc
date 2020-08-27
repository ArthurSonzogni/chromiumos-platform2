// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/icmp_tool.h"

#include <stdlib.h>

#include <base/strings/stringprintf.h>

#include "debugd/src/helper_utils.h"
#include "debugd/src/process_with_output.h"

using std::map;
using std::string;

namespace debugd {

string ICMPTool::TestICMP(const string& host) {
  map<string, string> options;
  return TestICMPWithOptions(host, options);
}

string ICMPTool::TestICMPWithOptions(const string& host,
                                     const map<string, string>& options) {
  string path;
  if (!GetHelperPath("icmp", &path))
    return "<path too long>";

  ProcessWithOutput p;
  if (!p.Init())
    return "<can't create process>";
  p.AddArg(path);

  for (const auto& option : options) {
    // No need to quote here because chromeos:ProcessImpl (base class of
    // ProcessWithOutput) passes arguments as is to helpers/icmp, which will
    // check arguments before executing in the shell.
    p.AddArg(base::StringPrintf("--%s=%s", option.first.c_str(),
                                option.second.c_str()));
  }

  p.AddArg(host);
  p.Run();
  string out;
  p.GetOutput(&out);
  return out;
}

}  // namespace debugd
