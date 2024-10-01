// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/logging.h>
#include <base/test/test_timeouts.h>
#include <brillo/syslog_logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  base::CommandLine::Init(argc, argv);
  brillo::InitLog(brillo::kLogToStderr);

  ::testing::InitGoogleTest(&argc, argv);
  ::testing::GTEST_FLAG(throw_on_failure) = true;

  // Default Mock class behavior to NiceMock to avoid spamming logs with
  // uninteresting calls. This should be before the InitGoogleMock() so that it
  // can be overridden with the flag passed in when running the test.
  ::testing::GMOCK_FLAG(default_mock_behavior) = 0;
  ::testing::InitGoogleMock(&argc, argv);

  TestTimeouts::Initialize();

  return RUN_ALL_TESTS();
}
