// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "update_engine/common/hwid_override.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace chromeos_update_engine {

class HwidOverrideTest : public ::testing::Test {
 public:
  HwidOverrideTest() {}
  HwidOverrideTest(const HwidOverrideTest&) = delete;
  HwidOverrideTest& operator=(const HwidOverrideTest&) = delete;

  ~HwidOverrideTest() override = default;

  void SetUp() override {
    ASSERT_TRUE(tempdir_.CreateUniqueTempDir());
    ASSERT_TRUE(base::CreateDirectory(tempdir_.GetPath().Append("etc")));
  }

 protected:
  base::ScopedTempDir tempdir_;
};

TEST_F(HwidOverrideTest, ReadGood) {
  std::string expected_hwid("expected");
  std::string keyval(HwidOverride::kHwidOverrideKey);
  keyval += ("=" + expected_hwid);
  ASSERT_EQ(base::WriteFile(tempdir_.GetPath().Append("etc/lsb-release"),
                            keyval.c_str(), keyval.length()),
            static_cast<int>(keyval.length()));
  EXPECT_EQ(expected_hwid, HwidOverride::Read(tempdir_.GetPath()));
}

TEST_F(HwidOverrideTest, ReadNothing) {
  std::string keyval("SOMETHING_ELSE=UNINTERESTING");
  ASSERT_EQ(base::WriteFile(tempdir_.GetPath().Append("etc/lsb-release"),
                            keyval.c_str(), keyval.length()),
            static_cast<int>(keyval.length()));
  EXPECT_EQ(std::string(), HwidOverride::Read(tempdir_.GetPath()));
}

TEST_F(HwidOverrideTest, ReadFailure) {
  EXPECT_EQ(std::string(), HwidOverride::Read(tempdir_.GetPath()));
}

}  // namespace chromeos_update_engine
