// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/syslog_logging.h>

#include "croslog/log_rotator/log_rotator.h"

int main(int argc, char* argv[]) {
  // Configure the log destination.
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  log_rotator::RotateStandardLogFiles();
}
