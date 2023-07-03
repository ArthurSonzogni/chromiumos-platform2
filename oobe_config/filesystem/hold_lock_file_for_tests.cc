// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A simple program that locks a file (given on the command line). Helper
// binary for file_handler_test.cc. It's necessary because we need a separate
// program that locks the file.

#include <optional>
#include <stdlib.h>

#include <base/files/file.h>
#include <base/logging.h>
#include <base/threading/platform_thread.h>
#include <brillo/syslog_logging.h>

#include "oobe_config/filesystem/file_handler.h"

int main(int argc, char** argv) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);

  if (argc != 2) {
    LOG(ERROR) << "Usage: hold_lock_file_for_tests file_name.\n"
               << "Locks the given file. It is expected the file to exist.";
    return -1;
  }

  base::FilePath lock_file_path(argv[1]);

  oobe_config::FileHandler file_handler;
  std::optional<base::File> lock_file = file_handler.OpenFile(lock_file_path);
  if (!lock_file.has_value()) {
    LOG(ERROR) << "Error opening " << lock_file_path.value() << ": "
               << base::File::ErrorToString(lock_file->error_details());
    return -2;
  }

  if (!file_handler.LockFileNoBlocking(*lock_file)) {
    PLOG(ERROR) << "Error locking " << lock_file_path.value() << ": ";
    return -2;
  }

  std::string lock_ready_msg = "file_is_locked";
  if (write(STDOUT_FILENO, lock_ready_msg.c_str(), lock_ready_msg.length()) !=
      lock_ready_msg.length()) {
    LOG(ERROR) << "Error writing msg.";
    return -2;
  }
  fsync(STDOUT_FILENO);

  // Normally, the parent unit test will kill us. But just in case the parent
  // crashes, eventually exit.
  base::PlatformThread::Sleep(base::Seconds(30));
  file_handler.UnlockFile(*lock_file);
  return 0;
}
