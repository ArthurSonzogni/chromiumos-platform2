// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <glib-unix.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/file_path.h>
#include <base/string_number_conversions.h>
#include <base/string_split.h>
#include <chromeos/syslog_logging.h>

#include "shill/dbus_control.h"
#include "shill/logging.h"
#include "shill/minijail.h"
#include "shill/shill_config.h"
#include "shill/shill_daemon.h"

using base::FilePath;
using std::string;
using std::vector;

namespace switches {

// Don't daemon()ize; run in foreground.
static const char kForeground[] = "foreground";
// Don't attempt to manage these devices.
static const char kDeviceBlackList[] = "device-black-list";
// Technologies to enable for portal check at startup.
static const char kPortalList[] = "portal-list";
// Flag that causes shill to show the help message and exit.
static const char kHelp[] = "help";
// Logging level:
//   0 = LOG(INFO), 1 = LOG(WARNING), 2 = LOG(ERROR),
//   -1 = SLOG(..., 1), -2 = SLOG(..., 2), etc.
static const char kLogLevel[] = "log-level";
// Scopes to enable for SLOG()-based logging.
static const char kLogScopes[] = "log-scopes";

// The help message shown if help flag is passed to the program.
static const char kHelpMessage[] = "\n"
    "Available Switches: \n"
    "  --foreground\n"
    "    Don\'t daemon()ize; run in foreground.\n"
    "  --device-black-list=device1,device2\n"
    "    Do not manage devices named device1 or device2\n"
    "  --log-level=N\n"
    "    Logging level:\n"
    "      0 = LOG(INFO), 1 = LOG(WARNING), 2 = LOG(ERROR),\n"
    "      -1 = SLOG(..., 1), -2 = SLOG(..., 2), etc.\n"
    "  --log-scopes=\"*scope1+scope2\".\n"
    "    Scopes to enable for SLOG()-based logging.\n"
    "  --portal-list=technology1,technology2\n"
    "    Specify technologies to perform portal detection on at startup.\n";
}  // namespace switches

namespace {

const char *kLoggerCommand = "/usr/bin/logger";
const char *kLoggerUser = "syslog";

}  // namespace

// Always logs to the syslog and logs to stderr if
// we are running in the foreground.
void SetupLogging(bool foreground, char *daemon_name) {
  int log_flags = 0;
  log_flags |= chromeos::kLogToSyslog;
  log_flags |= chromeos::kLogHeader;
  if (foreground) {
    log_flags |= chromeos::kLogToStderr;
  }
  chromeos::InitLog(log_flags);

  if (!foreground) {
    vector<char *> logger_command_line;
    int logger_stdin_fd;
    logger_command_line.push_back(const_cast<char *>(kLoggerCommand));
    logger_command_line.push_back(const_cast<char *>("--priority"));
    logger_command_line.push_back(const_cast<char *>("daemon.err"));
    logger_command_line.push_back(const_cast<char *>("--tag"));
    logger_command_line.push_back(daemon_name);
    logger_command_line.push_back(NULL);

    shill::Minijail *minijail = shill::Minijail::GetInstance();
    struct minijail *jail = minijail->New();
    minijail->DropRoot(jail, kLoggerUser);

    if (!minijail->RunPipeAndDestroy(jail, logger_command_line,
                                     NULL, &logger_stdin_fd)) {
      LOG(ERROR) << "Unable to spawn logger. "
                 << "Writes to stderr will be discarded.";
      return;
    }

    // Note that we don't set O_CLOEXEC here. This means that stderr
    // from any child processes will, by default, be logged to syslog.
    if (dup2(logger_stdin_fd, fileno(stderr)) != fileno(stderr)) {
      LOG(ERROR) << "Failed to redirect stderr to syslog: "
                 << strerror(errno);
    }
    close(logger_stdin_fd);
  }
}

void DeleteDBusControl(void* param) {
  SLOG(DBus, 2) << __func__;
  shill::DBusControl* dbus_control =
      reinterpret_cast<shill::DBusControl*>(param);
  delete dbus_control;
}

gboolean ExitSigHandler(gpointer data) {
  LOG(INFO) << "Shutting down due to received signal.";
  shill::Daemon* daemon = reinterpret_cast<shill::Daemon*>(data);
  daemon->Quit();
  return TRUE;
}


int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  CommandLine::Init(argc, argv);
  CommandLine* cl = CommandLine::ForCurrentProcess();

  if (cl->HasSwitch(switches::kHelp)) {
    LOG(INFO) << switches::kHelpMessage;
    return 0;
  }

  const int nochdir = 0, noclose = 0;
  if (!cl->HasSwitch(switches::kForeground))
    PLOG_IF(FATAL, daemon(nochdir, noclose) == -1) << "Failed to daemonize";

  SetupLogging(cl->HasSwitch(switches::kForeground), argv[0]);
  if (cl->HasSwitch(switches::kLogLevel)) {
    string log_level = cl->GetSwitchValueASCII(switches::kLogLevel);
    int level = 0;
    if (base::StringToInt(log_level, &level) &&
        level < logging::LOG_NUM_SEVERITIES) {
      logging::SetMinLogLevel(level);
      // Like VLOG, SLOG uses negative verbose level.
      shill::ScopeLogger::GetInstance()->set_verbose_level(-level);
    } else {
      LOG(WARNING) << "Bad log level: " << log_level;
    }
  }

  if (cl->HasSwitch(switches::kLogScopes)) {
    string log_scopes = cl->GetSwitchValueASCII(switches::kLogScopes);
    shill::ScopeLogger::GetInstance()->EnableScopesByName(log_scopes);
  }

  // TODO(pstew): This should be chosen based on config
  // Make sure we delete the DBusControl object AFTER the LazyInstances
  // since some LazyInstances destructors rely on D-Bus being around.
  shill::DBusControl* dbus_control = new shill::DBusControl();
  exit_manager.RegisterCallback(DeleteDBusControl, dbus_control);
  dbus_control->Init();

  shill::Config config;
  shill::Daemon daemon(&config, dbus_control);

  if (cl->HasSwitch(switches::kDeviceBlackList)) {
    vector<string> device_list;
    base::SplitString(cl->GetSwitchValueASCII(switches::kDeviceBlackList),
                      ',', &device_list);

    vector<string>::iterator i;
    for (i = device_list.begin(); i != device_list.end(); ++i) {
      daemon.AddDeviceToBlackList(*i);
    }
  }

  if (cl->HasSwitch(switches::kPortalList)) {
    daemon.SetStartupPortalList(cl->GetSwitchValueASCII(switches::kPortalList));
  }

  g_unix_signal_add(SIGINT, ExitSigHandler, &daemon);
  g_unix_signal_add(SIGTERM, ExitSigHandler, &daemon);

  daemon.Run();

  return 0;
}
