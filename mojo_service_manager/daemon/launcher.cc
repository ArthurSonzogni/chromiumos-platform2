// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Because of b/235922792 the service manager needs to restart each time
// Chrome restarts.
// Chrome could restart by:
//  1. Tast tests using session manager dbus method to restart Chrome for
//     testing.
//  2. Chrome crash and restarted by session manager.
//  3. UI job being restart.
//  4. Session manager quits because of logout and respawn by ui-respawn script.
//  5. Chrome crash too fast so session manager quits and respawn by ui-respawn
//     script.
// Note that the first two won't change the state of upstart ui job.
// To handle these situations, service manager quits each time the Chrome
// disconnects and this launcher will respawn it.

#include <array>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/process/process.h>
#include <brillo/syslog_logging.h>

#include "mojo_service_manager/daemon/constants.h"

namespace {

namespace service_manager = chromeos::mojo_service_manager;

// Binary to execute service manager.
constexpr char kServiceManagerBin[] = "/usr/bin/mojo_service_manager";

}  // namespace

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToStderr | brillo::kLogToSyslog);

  while (true) {
    brillo::ProcessImpl proc;
    proc.AddArg(kServiceManagerBin);
    for (int i = 1; i < argc; ++i) {
      proc.AddArg(argv[i]);
    }
    int exit_code = proc.Run();
    if (exit_code != 0)
      return exit_code;
    LOG(INFO)
        << "Respawning mojo_service_manager because browser disconnected.";
    base::FilePath socket_file{service_manager::kSocketPath};
    if (!base::DeleteFile(socket_file)) {
      PLOG(ERROR) << "Failed to delete socket file: " << socket_file;
      return 1;
    }
  }
}
