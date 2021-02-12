// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "secanomalyd/daemon.h"

int main(int argc, char* argv[]) {
  brillo::FlagHelper::Init(argc, argv, "CrOS monitor daemon");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  Daemon().Run();
  return 0;
}
