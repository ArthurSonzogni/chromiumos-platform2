// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/optional.h>
#include <base/strings/string_number_conversions.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"

namespace diagnostics {
namespace {

const auto kFileNameTest = "test";
const auto kFileNameTestInt = "test_int";
const auto kFileNameNotExist = "not_exist";

const auto kDataStr = "\r  test\n  ";
const auto kExpectedStr = "test";
const auto kDataNumber = "\r  42\n  ";
const auto kExpectedNumber = 42;

class FileUtilsTest : public BaseFileTest {
 protected:
  void SetUp() override {
    CreateTestRoot();
    SetFile(kFileNameTest, kDataStr);
    SetFile(kFileNameTestInt, kDataNumber);
  }
};

TEST_F(FileUtilsTest, ReadAndTrimString) {
  std::string str;
  ASSERT_TRUE(ReadAndTrimString(root_dir(), kFileNameTest, &str));
  EXPECT_EQ(str, kExpectedStr);
  ASSERT_TRUE(ReadAndTrimString(GetPathUnderRoot(kFileNameTest), &str));
  EXPECT_EQ(str, kExpectedStr);

  ASSERT_FALSE(ReadAndTrimString(root_dir(), kFileNameNotExist, &str));

  base::Optional<std::string> opt_str;
  ASSERT_TRUE(ReadAndTrimString(root_dir(), kFileNameTest, &opt_str));
  ASSERT_TRUE(opt_str.has_value());
  EXPECT_EQ(opt_str.value(), kExpectedStr);
}

TEST_F(FileUtilsTest, ReadInteger) {
  int num;
  ASSERT_TRUE(
      ReadInteger(root_dir(), kFileNameTestInt, &base::StringToInt, &num));
  EXPECT_EQ(num, kExpectedNumber);
  ASSERT_TRUE(ReadInteger(GetPathUnderRoot(kFileNameTestInt),
                          &base::StringToInt, &num));
  EXPECT_EQ(num, kExpectedNumber);

  ASSERT_FALSE(
      ReadInteger(root_dir(), kFileNameTest, &base::StringToInt, &num));
  ASSERT_FALSE(
      ReadInteger(root_dir(), kFileNameNotExist, &base::StringToInt, &num));
}

}  // namespace
}  // namespace diagnostics
