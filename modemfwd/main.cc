// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sysexits.h>

#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "modemfwd/daemon.h"

int main(int argc, char** argv) {
  DEFINE_string(helper_directory, "/opt/google/modemfwd-helpers",
                "Directory to load modem-specific helpers from");
  DEFINE_string(firmware_directory, "", "Directory to load firmware from");
  brillo::FlagHelper::Init(argc, argv, "Daemon which updates modem firmware.");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  if (FLAGS_helper_directory.empty()) {
    LOG(ERROR) << "Must supply helper directory";
    return EX_USAGE;
  }

  if (FLAGS_firmware_directory.empty()) {
    LOG(INFO) << "Running modemfwd with firmware component...";
    modemfwd::Daemon d(FLAGS_helper_directory);
    return d.Run();
  } else {
    LOG(INFO) << "Running modemfwd with firmware directory...";
    modemfwd::Daemon d(FLAGS_helper_directory, FLAGS_firmware_directory);
    return d.Run();
  }

  return EX_USAGE;
}
