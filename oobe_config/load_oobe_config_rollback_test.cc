// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/load_oobe_config_rollback.h"

#include <memory>
#include <string>

#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

#include "oobe_config/oobe_config.h"
#include "oobe_config/rollback_constants.h"

using base::ScopedTempDir;
using std::string;
using std::unique_ptr;

namespace oobe_config {

class LoadOobeConfigRollbackTest : public ::testing::Test {
 protected:
  void SetUp() override {
    oobe_config_ = std::make_unique<OobeConfig>();
    ASSERT_TRUE(fake_root_dir_.CreateUniqueTempDir());
    oobe_config_->set_prefix_path_for_testing(fake_root_dir_.GetPath());
    load_config_ = std::make_unique<LoadOobeConfigRollback>(
        oobe_config_.get(), /*allow_unencrypted=*/true);
  }

  base::ScopedTempDir fake_root_dir_;

  unique_ptr<LoadOobeConfigRollback> load_config_;
  unique_ptr<OobeConfig> oobe_config_;
};

TEST_F(LoadOobeConfigRollbackTest, SimpleTest) {
  string config, enrollment_domain;
  EXPECT_FALSE(load_config_->GetOobeConfigJson(&config, &enrollment_domain));
}

}  // namespace oobe_config
