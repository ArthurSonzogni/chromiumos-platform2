// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/load_oobe_config_flex.h"

#include <memory>

#include <gtest/gtest.h>

#include "oobe_config/filesystem/file_handler_for_testing.h"

namespace oobe_config {

const char kFlexConfig[] = "{ \"flexToken\": \"test_flex_token\" }";

class LoadOobeConfigFlexTest : public ::testing::Test {
 protected:
  void SetUp() override {
    load_config_ = std::make_unique<LoadOobeConfigFlex>(file_handler_);
  }

  std::unique_ptr<LoadOobeConfigFlex> load_config_;
  FileHandlerForTesting file_handler_;
};

TEST_F(LoadOobeConfigFlexTest, NoFlexConfig) {
  std::string config;
  ASSERT_FALSE(load_config_->GetOobeConfigJson(&config));
  ASSERT_EQ(config, "");
}

TEST_F(LoadOobeConfigFlexTest, FlexConfigPresent) {
  file_handler_.CreateFlexConfigDirectory();
  file_handler_.WriteFlexConfigData(kFlexConfig);
  std::string config;

  ASSERT_TRUE(load_config_->GetOobeConfigJson(&config));
  ASSERT_EQ(config, kFlexConfig);
}
}  // namespace oobe_config
