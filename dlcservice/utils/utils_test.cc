// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dlcservice/utils/mock_utils.h"
#include "dlcservice/utils/utils.h"

using testing::_;
using testing::StrictMock;

namespace dlcservice {

TEST(UtilsTest, LogicalVolumeNameTest) {
  Utils u;
  EXPECT_EQ("dlc_sample-dlc_a",
            u.LogicalVolumeName("sample-dlc", PartitionSlot::A));
  EXPECT_EQ("dlc_sample-dlc_b",
            u.LogicalVolumeName("sample-dlc", PartitionSlot::B));
}

class FunctionsTest : public testing::Test {
 public:
  FunctionsTest() = default;
  FunctionsTest(const FunctionsTest&) = delete;
  FunctionsTest& operator=(const FunctionsTest&) = delete;

  void SetUp() override {
    mu_ = std::make_unique<StrictMock<MockUtils>>();
    mu_ptr_ = mu_.get();
  }

 protected:
  MockUtils* mu_ptr_ = nullptr;
  std::unique_ptr<MockUtils> mu_;
};

TEST_F(FunctionsTest, LogicalVolumeNameFunctionsTest) {
  EXPECT_CALL(*mu_ptr_, LogicalVolumeName(_, _));
  (void)LogicalVolumeName("", {}, std::move(mu_));
}

TEST_F(FunctionsTest, HashFileFunctionsTest) {
  EXPECT_CALL(*mu_ptr_, HashFile(_, _, _, _));
  (void)HashFile(base::FilePath(), 0, nullptr, false, std::move(mu_));
}

TEST_F(FunctionsTest, GetDlcManifestTest) {
  EXPECT_CALL(*mu_ptr_, GetDlcManifest(_, _, _));
  (void)GetDlcManifest(base::FilePath(), "", "", std::move(mu_));
}

}  // namespace dlcservice
