// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// based on pam_google_testrunner.cc

#include <xz.h>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <brillo/test_helpers.h>
#include <gtest/gtest.h>

#include "update_engine/common/terminator.h"
#include "update_engine/payload_generator/xz.h"

int main(int argc, char** argv) {
  LOG(INFO) << "started";
  base::AtExitManager exit_manager;
  // xz-embedded requires to initialize its CRC-32 table once on startup.
  xz_crc32_init();
  // The LZMA SDK-based Xz compressor used in the payload generation requires
  // this one-time initialization.
  chromeos_update_engine::XzCompressInit();
  // TODO(garnold) temporarily cause the unittest binary to exit with status
  // code 2 upon catching a SIGTERM. This will help diagnose why the unittest
  // binary is perceived as failing by the buildbot.  We should revert it to use
  // the default exit status of 1.  Corresponding reverts are necessary in
  // terminator_unittest.cc.
  chromeos_update_engine::Terminator::Init(2);
  LOG(INFO) << "parsing command line arguments";
  base::CommandLine::Init(argc, argv);
  LOG(INFO) << "initializing gtest";
  SetUpTests(&argc, argv, true);
  // Logging to string is not thread safe.
  brillo::LogToString(false);
  LOG(INFO) << "running unit tests";
  int test_result = RUN_ALL_TESTS();
  LOG(INFO) << "unittest return value: " << test_result;
  return test_result;
}
