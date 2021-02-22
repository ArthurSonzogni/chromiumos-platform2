// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/daemon.h"
#include "minios/minios.h"

int main() {
  logging::InitLogging({
      .logging_dest = logging::LOG_TO_ALL,
      .log_file_path = minios::kDebugConsole,
      .lock_log = logging::DONT_LOCK_LOG_FILE,
  });
  return minios::Daemon().Run();
}
