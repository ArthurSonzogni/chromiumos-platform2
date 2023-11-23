// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <base/logging.h>
#include <brillo/syslog_logging.h>

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToStderr);
  LOG(INFO) << "Heart tool";

  return EXIT_SUCCESS;
}
