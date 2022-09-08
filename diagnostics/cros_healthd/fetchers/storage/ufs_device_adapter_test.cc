// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include <gtest/gtest.h>

#include "diagnostics/common/statusor.h"
#include "diagnostics/cros_healthd/fetchers/storage/ufs_device_adapter.h"

namespace diagnostics {
namespace {

TEST(EmmcDeviceAdapterTest, OkData) {
  constexpr char kPath[] =
      "cros_healthd/fetchers/storage/testdata/sys/block/sda";
  UfsDeviceAdapter adapter{base::FilePath(kPath)};

  ASSERT_TRUE(adapter.GetVendorId().ok());
  ASSERT_TRUE(adapter.GetProductId().ok());
  ASSERT_TRUE(adapter.GetRevision().ok());
  ASSERT_TRUE(adapter.GetModel().ok());
  ASSERT_TRUE(adapter.GetFirmwareVersion().ok());

  ASSERT_TRUE(adapter.GetVendorId().value().is_jedec_manfid());
  ASSERT_TRUE(adapter.GetProductId().value().is_other());
  ASSERT_TRUE(adapter.GetRevision().value().is_other());
  ASSERT_TRUE(adapter.GetFirmwareVersion().value().is_ufs_fwrev());

  EXPECT_EQ("sda", adapter.GetDeviceName());
  EXPECT_EQ(0x1337, adapter.GetVendorId().value().get_jedec_manfid());
  EXPECT_EQ(0, adapter.GetProductId().value().get_other());
  EXPECT_EQ(0, adapter.GetRevision().value().get_other());
  EXPECT_EQ("MYUFS", adapter.GetModel().value());
  EXPECT_EQ(0x32323032, adapter.GetFirmwareVersion().value().get_ufs_fwrev());
}

TEST(EmmcDeviceAdapterTest, NoData) {
  constexpr char kPath[] =
      "cros_healthd/fetchers/storage/testdata/sys/block/sdb";
  UfsDeviceAdapter adapter{base::FilePath(kPath)};

  ASSERT_FALSE(adapter.GetVendorId().ok());
  ASSERT_TRUE(adapter.GetProductId().ok());
  ASSERT_TRUE(adapter.GetRevision().ok());
  ASSERT_FALSE(adapter.GetModel().ok());
  ASSERT_FALSE(adapter.GetFirmwareVersion().ok());

  ASSERT_TRUE(adapter.GetProductId().value().is_other());
  ASSERT_TRUE(adapter.GetRevision().value().is_other());

  EXPECT_EQ("sdb", adapter.GetDeviceName());
  EXPECT_EQ(0, adapter.GetProductId().value().get_other());
  EXPECT_EQ(0, adapter.GetRevision().value().get_other());
}

}  // namespace
}  // namespace diagnostics
