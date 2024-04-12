// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <base/command_line.h>
#include <base/functional/bind.h>
#include <base/logging.h>
#include <brillo/minijail/minijail.h>
#include <brillo/syslog_logging.h>

#include "shill/daemon_task.h"
#include "shill/error.h"
#include "shill/logging.h"
#include "shill/shill_config.h"
#include "shill/shill_daemon.h"
#include "shill/technology.h"

namespace {

namespace switches {

// Don't daemon()ize; run in foreground.
const char kForeground[] = "foreground";
// Flag that causes shill to show the help message and exit.
const char kHelp[] = "help";

// The help message shown if help flag is passed to the program.
const char kHelpMessage[] =
    "\n"
    "Available Switches: \n"
    "  --foreground\n"
    "    Don\'t daemon()ize; run in foreground.\n"
    "  --log-level=N\n"
    "    Logging level:\n"
    "      0 = LOG(INFO), 1 = LOG(WARNING), 2 = LOG(ERROR),\n"
    "      -1 = SLOG(..., 1), -2 = SLOG(..., 2), etc.\n"
    "  --log-scopes=\"*scope1+scope2\".\n"
    "    Scopes to enable for SLOG()-based logging.\n";
}  // namespace switches

const char kLoggerCommand[] = "/usr/bin/logger";
const char kLoggerUser[] = "syslog";

// Always logs to the syslog and logs to stderr if
// we are running in the foreground.
void SetupLogging(bool foreground, const char* daemon_name) {
  int log_flags = 0;
  log_flags |= brillo::kLogToSyslog;
  log_flags |= brillo::kLogHeader;
  if (foreground) {
    log_flags |= brillo::kLogToStderr;
  }
  brillo::InitLog(log_flags);

  if (!foreground) {
    std::vector<char*> logger_command_line;
    int logger_stdin_fd;
    logger_command_line.push_back(const_cast<char*>(kLoggerCommand));
    logger_command_line.push_back(const_cast<char*>("--priority"));
    logger_command_line.push_back(const_cast<char*>("daemon.err"));
    logger_command_line.push_back(const_cast<char*>("--tag"));
    logger_command_line.push_back(const_cast<char*>(daemon_name));
    logger_command_line.push_back(nullptr);

    brillo::Minijail* minijail = brillo::Minijail::GetInstance();
    struct minijail* jail = minijail->New();
    minijail->DropRoot(jail, kLoggerUser, kLoggerUser);

    if (!minijail->RunPipeAndDestroy(jail, logger_command_line, nullptr,
                                     &logger_stdin_fd)) {
      LOG(ERROR) << "Unable to spawn logger. "
                 << "Writes to stderr will be discarded.";
      return;
    }

    // Note that we don't set O_CLOEXEC here. This means that stderr
    // from any child processes will, by default, be logged to syslog.
    if (dup2(logger_stdin_fd, fileno(stderr)) != fileno(stderr)) {
      PLOG(ERROR) << "Failed to redirect stderr to syslog";
    }
    close(logger_stdin_fd);
  }
}

}  // namespace

int main(int argc, char** argv) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();

  if (cl->HasSwitch(switches::kHelp)) {
    LOG(INFO) << switches::kHelpMessage;
    return 0;
  }

  shill::Config config;
  // Construct the daemon first, so we get our AtExitManager.
  shill::ShillDaemon daemon(&config);

  // Configure logging before we start anything else, so early log messages go
  // to a consistent place.
  SetupLogging(cl->HasSwitch(switches::kForeground), argv[0]);
  auto log_config_path = base::FilePath(config.GetStorageDirectory())
                             .Append(shill::kLogOverrideFile);
  if (!shill::ApplyOverrideLogConfig(log_config_path)) {
    shill::SetLogLevelFromCommandLine(cl);
  }

  // Go for it!
  daemon.Run();

  LOG(INFO) << "Process exiting.";

  return 0;
}
