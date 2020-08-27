// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/battery_tool.h"

#include <unistd.h>

#include "debugd/src/process_with_id.h"
#include "debugd/src/process_with_output.h"

namespace {

const char kBatteryFirmware[] = "/usr/sbin/ec_sb_firmware_update";
const char kEcTool[] = "/usr/sbin/ectool";
const char kUnsupportedMessage[] =
    "Sorry, but this command is unavailable on this device.";

}  // namespace

namespace debugd {

std::string BatteryTool::BatteryFirmware(const std::string& option) {
  const char* tool_name;
  std::string output;
  ProcessWithOutput process;
  // Disabling sandboxing since battery requires higher privileges.
  process.DisableSandbox();
  if (!process.Init())
    return "<process init failed>";

  if (option == "info") {
    tool_name = kEcTool;
    process.AddArg(tool_name);
    process.AddArg("battery");
  } else if (option == "update") {
    tool_name = kBatteryFirmware;
    process.AddArg(tool_name);
    process.AddArg("update");
  } else if (option == "check") {
    tool_name = kBatteryFirmware;
    process.AddArg(tool_name);
    process.AddArg("check");
  } else {
    return "<process invalid option>";
  }
  if (access(tool_name, F_OK) != 0)
    return kUnsupportedMessage;

  process.Run();
  process.GetOutput(&output);
  return output;
}

}  // namespace debugd
