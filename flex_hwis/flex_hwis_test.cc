// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flex_hwis/flex_hwis.h"
#include <optional>

#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace flex_hwis {

class FlexHwisTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CHECK(test_dir_.CreateUniqueTempDir());
    test_path_ = test_dir_.GetPath();
    flex_hwis_sender_ = flex_hwis::FlexHwisSender(test_path_);
  }

  std::optional<flex_hwis::FlexHwisSender> flex_hwis_sender_;
  base::ScopedTempDir test_dir_;
  base::FilePath test_path_;
};

TEST_F(FlexHwisTest, Init) {
  EXPECT_EQ(flex_hwis_sender_->CollectAndSend(), Result::Sent);
}

}  // namespace flex_hwis
