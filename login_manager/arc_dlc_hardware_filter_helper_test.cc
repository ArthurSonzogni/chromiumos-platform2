// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/arc_dlc_hardware_filter_helper.h"

#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace login_manager {

class ArcDlcHardwareFilterHelperTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(test_dir_.CreateUniqueTempDir());
    test_path_ = test_dir_.GetPath();
  }

  // Helper function to create a file with specified content.
  bool CreateFile(const base::FilePath& path, const std::string& content) {
    if (!base::CreateDirectory(path.DirName())) {
      return false;
    }
    return base::WriteFile(path, content);
  }

  base::ScopedTempDir test_dir_;
  base::FilePath test_path_;
};

TEST_F(ArcDlcHardwareFilterHelperTest, GetPciClass) {
  // The PCI class ID is the third byte (from the right).
  // 0x030000 -> 0x03
  EXPECT_EQ(0x03, ArcDlcHardwareFilterHelper::GetPciClass(0x030000));
  // 0x020000 -> 0x02
  EXPECT_EQ(0x02, ArcDlcHardwareFilterHelper::GetPciClass(0x020000));
}

TEST_F(ArcDlcHardwareFilterHelperTest, ReadAndTrimString) {
  const base::FilePath file_path = test_path_.Append("test_file");
  ASSERT_TRUE(CreateFile(file_path, "   test content   \n"));

  auto result = ArcDlcHardwareFilterHelper::ReadAndTrimString(file_path);
  EXPECT_EQ("test content", result.value());

  // Test case for a file that does not exist.
  auto not_exist_result = ArcDlcHardwareFilterHelper::ReadAndTrimString(
      test_path_.Append("not_exist"));
  EXPECT_FALSE(not_exist_result.has_value());
}

// Test `ReadHexStringToUint16` function.
TEST_F(ArcDlcHardwareFilterHelperTest, ReadHexStringToUint16) {
  const base::FilePath file_path = test_path_.Append("hex_file_16");
  ASSERT_TRUE(CreateFile(file_path, "0x8086"));

  auto result = ArcDlcHardwareFilterHelper::ReadHexStringToUint16(file_path);
  EXPECT_EQ(0x8086, result.value());

  // Test for an invalid hex string.
  ASSERT_TRUE(CreateFile(file_path, "invalid_hex"));
  auto invalid_result =
      ArcDlcHardwareFilterHelper::ReadHexStringToUint16(file_path);
  EXPECT_FALSE(invalid_result.has_value());

  // Test for an overflow value (larger than uint16_t).
  ASSERT_TRUE(
      CreateFile(file_path, "0x10000"));  // 65536, which is > max uint16_t.
  auto overflow_result =
      ArcDlcHardwareFilterHelper::ReadHexStringToUint16(file_path);
  EXPECT_FALSE(overflow_result.has_value());
}

// Test `ReadHexStringToUint32` function.
TEST_F(ArcDlcHardwareFilterHelperTest, ReadHexStringToUint32) {
  const base::FilePath file_path = test_path_.Append("hex_file_32");
  ASSERT_TRUE(CreateFile(file_path, "0x030000"));

  auto result = ArcDlcHardwareFilterHelper::ReadHexStringToUint32(file_path);
  EXPECT_EQ(0x030000, result.value());

  // Test for an invalid hex string.
  ASSERT_TRUE(CreateFile(file_path, "invalid_hex"));
  auto invalid_result =
      ArcDlcHardwareFilterHelper::ReadHexStringToUint32(file_path);
  EXPECT_FALSE(invalid_result.has_value());
}

// Test `ReadStringToInt` function.
TEST_F(ArcDlcHardwareFilterHelperTest, ReadStringToInt) {
  const base::FilePath file_path = test_path_.Append("int_file");
  ASSERT_TRUE(CreateFile(file_path, "12345"));

  auto result = ArcDlcHardwareFilterHelper::ReadStringToInt(file_path);
  EXPECT_EQ(12345, result.value());

  // Test for an invalid integer string.
  ASSERT_TRUE(CreateFile(file_path, "not_an_int"));
  auto invalid_result = ArcDlcHardwareFilterHelper::ReadStringToInt(file_path);
  EXPECT_FALSE(invalid_result.has_value());
}

// Test `ParseIomemContent` function.
TEST_F(ArcDlcHardwareFilterHelperTest, ParseIomemContent) {
  // Test case for a valid /proc/iomem content with 8GB RAM.
  const std::string iomem_content_8gb =
      "00001000-1fffffff : Reserved\n"
      "20000000-21fffffff : System RAM\n";
  auto result_8gb =
      ArcDlcHardwareFilterHelper::ParseIomemContent(iomem_content_8gb);
  EXPECT_EQ(base::GiB(8).InBytes(), result_8gb.value());

  // Test case for a valid /proc/iomem content with 4GB RAM.
  const std::string iomem_content_4gb = "00000000-0ffffffff : System RAM\n";
  auto result_4gb =
      ArcDlcHardwareFilterHelper::ParseIomemContent(iomem_content_4gb);
  EXPECT_EQ(base::GiB(4).InBytes(), result_4gb.value());

  // Test case for an empty content string.
  auto empty_result = ArcDlcHardwareFilterHelper::ParseIomemContent("");
  EXPECT_FALSE(empty_result.has_value());

  // Test case for an invalidly formatted content string.
  auto invalid_result =
      ArcDlcHardwareFilterHelper::ParseIomemContent("invalid format");
  EXPECT_FALSE(invalid_result.has_value());
}

}  // namespace login_manager
