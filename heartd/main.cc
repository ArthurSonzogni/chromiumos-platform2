// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/syslog_logging.h>

#include "heartd/daemon/heartd.h"
#include "heartd/minijail/minijail_configuration.h"

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  heartd::EnterHeartdMinijail();
  auto heartd = heartd::HeartdDaemon();
  return heartd.Run();
}
