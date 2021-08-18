// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check.h>
#include <base/check_op.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "fusebox/fuse_frontend.h"

using base::CommandLine;

namespace {

void SetupLogging() {
  brillo::InitLog(brillo::kLogToStderr);
}

}  // namespace

int main(int argc, char** argv) {
  CommandLine::Init(argc, argv);
  SetupLogging();
  LOG(INFO) << argv[0];
  return 0;
}
