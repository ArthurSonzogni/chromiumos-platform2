// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common-mk/testrunner.h"

#include "brillo/syslog_logging.h"

int main(int argc, char** argv) {
  // Initialize logging so tests would be able to test logs by calling
  //   brillo::LogToString(true);
  //   brillo::ClearLog();
  //   // Code that produces logs...
  //   // Checks that examine logs from brillo::GetLog()...
  brillo::InitLog(brillo::kLogToStderr);

  auto runner = platform2::TestRunner(argc, argv);
  return runner.Run();
}
