// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING:
// This callback is intended to be a legacy entry point.  New scripts should not
// be added here.  Instead a proper UI should be created to manage the system
// interactions.

#include "debugd/src/shill_scripts_tool.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/ptr_util.h>

#include "debugd/src/error_utils.h"
#include "debugd/src/process_with_id.h"

namespace debugd {

namespace {

const char kUnsupportedShillScriptToolErrorName[] =
    "org.chromium.debugd.error.UnsupportedShillScriptTool";

const char kUser[] = "shill-scripts";
const char kGroup[] = "shill-scripts";

// Where shill scripts are installed.
const char kScriptsDir[] = "/usr/bin";

// clang-format off
const char * const kWhitelistedScripts[] = {
    "connectivity",
    "ff_debug",
    "modem",
    "network_diag",
    "set_apn",
    "set_arpgw",
    "set_cellular_ppp",
    "set_wake_on_lan",
    "wpa_debug",
};
// clang-format on

// Only permit certain scripts here.
bool WhitelistedScript(const std::string& script, brillo::ErrorPtr* error) {
  for (const char* listed : kWhitelistedScripts)
    if (script == listed)
      return true;

  DEBUGD_ADD_ERROR(error, kUnsupportedShillScriptToolErrorName, script.c_str());
  return false;
}

}  // namespace

bool ShillScriptsTool::Run(const dbus::FileDescriptor& outfd,
                           const std::string& script,
                           const std::vector<std::string>& script_args,
                           std::string* out_id,
                           brillo::ErrorPtr* error) {
  if (!WhitelistedScript(script, error))
    return false;

  auto p = base::MakeUnique<ProcessWithId>();
  p->SandboxAs(kUser, kGroup);
  p->Init();

  const base::FilePath dir(kScriptsDir);
  p->AddArg(dir.Append(script).value());

  for (const auto& arg : script_args)
    p->AddArg(arg);

  p->BindFd(outfd.value(), STDOUT_FILENO);
  p->BindFd(outfd.value(), STDERR_FILENO);
  *out_id = p->id();
  p->Start();

  RecordProcess(std::move(p));

  return true;
}

}  // namespace debugd
