// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <optional>

#include <blkid/blkid.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "minios/blkid_wrapper.h"

namespace minios {
using ::testing::Optional;

class BlkidTest : public ::testing::Test {
 public:
  void SetUp() override { wrapper_ = std::make_unique<BlkIdWrapper>(); }

 protected:
  std::unique_ptr<BlkIdWrapper> wrapper_;
};

TEST_F(BlkidTest, VerifyGetDevice) {
  EXPECT_FALSE(wrapper_->HandleGetDevice(nullptr));
  EXPECT_TRUE(wrapper_->HandleGetDevice(reinterpret_cast<blkid_dev>(0x12345)));
}

TEST_F(BlkidTest, VerifyGetTagHandler) {
  EXPECT_EQ(wrapper_->HandleTagValue(nullptr, "tag", "device"), std::nullopt);
  const std::string tag_value = "useful tag value";
  EXPECT_THAT(wrapper_->HandleTagValue(tag_value.c_str(), "tag", "device"),
              Optional(tag_value));
}

}  // namespace minios
