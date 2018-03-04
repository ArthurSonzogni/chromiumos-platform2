// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* Test for the CrosConfig Fake */

#include <base/logging.h>
#include <chromeos-config/libcros_config/fake_cros_config.h>
#include <gtest/gtest.h>

class FakeCrosConfigTest : public testing::Test {
 protected:
  brillo::FakeCrosConfig cros_config_;
};

TEST_F(FakeCrosConfigTest, CheckGetString) {
  std::string val;

  ASSERT_FALSE(cros_config_.GetString("/", "wallpaper", &val));

  cros_config_.SetString("/", "wallpaper", "testing");
  ASSERT_TRUE(cros_config_.GetString("/", "wallpaper", &val));
  ASSERT_EQ("testing", val);

  // Try a non-root node.
  cros_config_.SetString("/thermal", "dptf-dv", "testing");
  ASSERT_TRUE(cros_config_.GetString("/thermal", "dptf-dv", &val));
}

TEST_F(FakeCrosConfigTest, CheckGetAbsPath) {
  std::string val;

  cros_config_.SetString("/thermal", "dptf-dv", "testing.dv");
  ASSERT_FALSE(cros_config_.GetAbsPath("/thermal", "dptf-dv", &val));

  // Add to the map and try again. This should work.
  cros_config_.SetTargetDir("dptf-dv", "/etc/dptf");
  ASSERT_TRUE(cros_config_.GetAbsPath("/thermal", "dptf-dv", &val));
  ASSERT_EQ("/etc/dptf/testing.dv", val);
}

int main(int argc, char** argv) {
  logging::LoggingSettings settings;
  settings.logging_dest = logging::LOG_TO_FILE;
  settings.log_file = "log.test";
  settings.lock_log = logging::DONT_LOCK_LOG_FILE;
  settings.delete_old = logging::APPEND_TO_OLD_LOG_FILE;
  logging::InitLogging(settings);

  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
