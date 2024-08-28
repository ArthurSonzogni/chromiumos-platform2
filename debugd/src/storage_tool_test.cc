// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/storage_tool.h"

#include <stdlib.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <gtest/gtest.h>

namespace {

constexpr char kTypeFileDataTarget[] = "/sys/devices/target/type";

constexpr char kTypeFileDataMMC[] = "/sys/devices/mmc_host/mmc0/type";

TEST(StorageToolTest, TestIsSupportedNoTypeLink) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath typeFile = temp_dir.GetPath().Append("type");
  base::FilePath vendFile = temp_dir.GetPath().Append("vendor");

  debugd::StorageTool sTool;
  std::string msg;
  bool supported = sTool.IsSupported(typeFile, vendFile, &msg);
  EXPECT_FALSE(supported);
  EXPECT_STREQ(msg.c_str(), "<Failed to read device type link>");
}

TEST(StorageToolTest, TestIsSupportedMMC) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath typeFile = temp_dir.GetPath().Append("mmc_type");
  base::FilePath vendFile = temp_dir.GetPath().Append("vendor");
  base::WriteFile(typeFile, kTypeFileDataMMC);

  debugd::StorageTool sTool;
  std::string msg;
  bool supported = sTool.IsSupported(typeFile, vendFile, &msg);
  EXPECT_FALSE(supported);
  EXPECT_STREQ(msg.c_str(), "<This feature is not supported>");
}

TEST(StorageToolTest, TestIsSupportedNoVend) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath typeFile = temp_dir.GetPath().Append("target_type");
  base::FilePath vendFile = temp_dir.GetPath().Append("vendor");
  base::WriteFile(typeFile, kTypeFileDataTarget);

  debugd::StorageTool sTool;
  std::string msg;
  bool supported = sTool.IsSupported(typeFile, vendFile, &msg);
  EXPECT_FALSE(supported);
  EXPECT_STREQ(msg.c_str(), "<Failed to open vendor file>");
}

TEST(StorageToolTest, TestIsSupportedVendEmpty) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath typeFile = temp_dir.GetPath().Append("target_type");
  base::FilePath vendFile = temp_dir.GetPath().Append("vendor");
  base::WriteFile(typeFile, kTypeFileDataTarget);

  base::WriteFile(vendFile, "");

  debugd::StorageTool sTool;
  std::string msg;
  bool supported = sTool.IsSupported(typeFile, vendFile, &msg);
  EXPECT_FALSE(supported);
  EXPECT_STREQ(msg.c_str(), "<Failed to find device type>");
}

TEST(StorageToolTest, TestIsSupportedOther) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath typeFile = temp_dir.GetPath().Append("target_type");
  base::FilePath vendFile = temp_dir.GetPath().Append("vendor");
  base::WriteFile(typeFile, kTypeFileDataTarget);

  base::WriteFile(vendFile, "OTHER");

  debugd::StorageTool sTool;
  std::string msg;
  bool supported = sTool.IsSupported(typeFile, vendFile, &msg);
  EXPECT_FALSE(supported);
  EXPECT_STREQ(msg.c_str(), "<This feature is not supported>");
}

TEST(StorageToolTest, TestIsSupportedATA) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath typeFile = temp_dir.GetPath().Append("target_type");
  base::FilePath vendFile = temp_dir.GetPath().Append("vendor");
  base::WriteFile(typeFile, kTypeFileDataTarget);

  base::WriteFile(vendFile, "ATA");

  debugd::StorageTool sTool;
  std::string msg;
  bool supported = sTool.IsSupported(typeFile, vendFile, &msg);
  EXPECT_TRUE(supported);
  EXPECT_STREQ(msg.c_str(), "");
}

}  // namespace
