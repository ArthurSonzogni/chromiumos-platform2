// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>

#include <brillo/syslog_logging.h>
#include <mojo/core/embedder/embedder.h>

#include "heartd/daemon/heartd.h"
#include "heartd/minijail/minijail_configuration.h"

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  mojo::core::Init();

  auto sysrq_fd = open("/proc/sysrq-trigger", O_WRONLY);
  if (sysrq_fd == -1) {
    LOG(ERROR) << "Failed to open /proc/sysrq-trigger";
  }

  heartd::EnterHeartdMinijail();
  auto heartd = heartd::HeartdDaemon(sysrq_fd);
  return heartd.Run();
}
