// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>

#include "minios/daemon.h"
#include "minios/utils.h"

int main() {
  logging::InitLogging(logging::LoggingSettings{
      .logging_dest = logging::LOG_TO_ALL,
      .log_file_path = minios::kLogFilePath,
  });

  return minios::Daemon().Run();
}
