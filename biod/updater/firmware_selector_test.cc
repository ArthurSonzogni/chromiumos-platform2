// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/updater/firmware_selector.h"

#include <base/check.h>
#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <gtest/gtest.h>

namespace biod {

class FirmwareSelectorTest : public testing::Test {
 protected:
  bool TouchFile(const base::FilePath& abspath) const {
    base::File file(abspath,
                    base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
    EXPECT_TRUE(file.IsValid());
    file.Close();

    EXPECT_TRUE(base::PathExists(abspath));
    return true;
  }

  FirmwareSelectorTest() = default;
  ~FirmwareSelectorTest() override = default;
};

TEST_F(FirmwareSelectorTest, GetProductionFirmwarePath) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  biod::FirmwareSelector selector(temp_dir.GetPath());
  base::FilePath expected_path("/opt/google/biod/fw");

  EXPECT_EQ(selector.GetFirmwarePath(), expected_path);
}

TEST_F(FirmwareSelectorTest, GetTestFirmwarePath) {
  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  biod::FirmwareSelector selector(temp_dir.GetPath());
  base::FilePath expected_path("/opt/google/biod/fw/test");

  EXPECT_TRUE(TouchFile(temp_dir.GetPath().Append(".use_test_firmware")));
  EXPECT_EQ(selector.GetFirmwarePath(), expected_path);
}

}  // namespace biod
