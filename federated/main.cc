// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "federated/daemon.h"

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);
  if (argc != 1) {
    LOG(ERROR) << "Unexpected command line arguments";
    return 1;
  }

  federated::Daemon daemon;
  daemon.Run();
  return 0;
}
