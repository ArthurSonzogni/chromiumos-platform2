// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <iostream>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/flag_helper.h>
#include <brillo/process/process.h>
#include <brillo/syslog_logging.h>

#include "diagnostics/cros_minidiag/elog_manager.h"

namespace {
constexpr const char kElogTool[] = "elogtool";
constexpr const char kList[] = "list";

int GetElogtoolString(std::string& output) {
  brillo::ProcessImpl elogtool;
  elogtool.SetSearchPath(true);
  elogtool.AddArg(kElogTool);
  elogtool.AddArg(kList);
  elogtool.RedirectOutputToMemory(true);

  output = "";
  const int result = elogtool.Run();
  if (result == 0)
    output = elogtool.GetOutputString(STDOUT_FILENO);
  return result;
}
}  // namespace

int main(int argc, char* argv[]) {
  brillo::FlagHelper::Init(argc, argv, "Cros MiniDiag Tool");

  // Dump the full elogtool list result.
  std::string elogtool_output;
  if (GetElogtoolString(elogtool_output) != 0) {
    LOG(ERROR) << "elogtool failed";
    return EXIT_FAILURE;
  }

  cros_minidiag::ElogManager elog_manager(elogtool_output);

  return EXIT_SUCCESS;
}
