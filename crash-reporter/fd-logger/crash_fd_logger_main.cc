// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A standalone tool for detecting and logging processes that are using many
// file descriptors, possibly because of a file descriptor leak.

#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "crash-reporter/fd-logger/crash_fd_logger.h"

int main(int argc, char* argv[]) {
  brillo::FlagHelper::Init(argc, argv, "ChromeOS file descriptor usage logger");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderr);

  fd_logger::LogOpenFilesInSystem();
  return 0;
}
