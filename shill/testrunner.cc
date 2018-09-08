// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/at_exit.h>
#include <base/command_line.h>
#include <chromeos/syslog_logging.h>
#include <gtest/gtest.h>

#include "shill/logging.h"

namespace switches {

static const char kHelp[] = "help";

static const char kHelpMessage[] = "\n"
    "Additional (non-gtest) switches:\n"
    "  --log-level=N\n"
    "    Logging level:\n"
    "      0 = LOG(INFO), 1 = LOG(WARNING), 2 = LOG(ERROR),\n"
    "      -1 = SLOG(..., 1), -2 = SLOG(..., 2), etc.\n"
    "  --log-scopes=\"*scope1+scope2\".\n"
    "    Scopes to enable for SLOG()-based logging.\n";

}  // namespace switches

int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  chromeos::InitLog(chromeos::kLogToStderr);
  shill::SetLogLevelFromCommandLine(cl);
  ::testing::InitGoogleTest(&argc, argv);

  if (cl->HasSwitch(switches::kHelp)) {
    std::cerr << switches::kHelpMessage;
  }

  return RUN_ALL_TESTS();
}
