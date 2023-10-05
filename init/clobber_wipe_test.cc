// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/clobber_wipe.h"

#include <limits.h>
#include <stdlib.h>
#include <sys/sysmacros.h>

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/blkdev_utils/mock_lvm.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/files/file_util.h>
#include <gtest/gtest.h>
#include <libcrossystem/crossystem.h>
#include <libcrossystem/crossystem_fake.h>
#include <libdlcservice/mock_utils.h>
#include <libdlcservice/utils.h>

#include "gmock/gmock.h"

#include "init/clobber_wipe_mock.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::StrictMock;

bool CreateDirectoryAndWriteFile(const base::FilePath& path,
                                 const std::string& contents) {
  return base::CreateDirectory(path.DirName()) &&
         base::WriteFile(path, contents.c_str(), contents.length()) ==
             contents.length();
}

class IsRotationalTest : public ::testing::Test {
 protected:
  IsRotationalTest() : clobber_ui_(DevNull()), clobber_wipe_(&clobber_ui_) {}

  void SetUp() override {
    ASSERT_TRUE(fake_dev_.CreateUniqueTempDir());
    ASSERT_TRUE(fake_sys_.CreateUniqueTempDir());
    clobber_wipe_.SetDevForTest(fake_dev_.GetPath());
    clobber_wipe_.SetSysForTest(fake_sys_.GetPath());
  }

  ClobberUi clobber_ui_;
  ClobberWipeMock clobber_wipe_;
  base::ScopedTempDir fake_dev_;
  base::ScopedTempDir fake_sys_;
};

TEST_F(IsRotationalTest, NonExistentDevice) {
  EXPECT_FALSE(
      clobber_wipe_.IsRotational(fake_dev_.GetPath().Append("nvme0n1p3")));
}

TEST_F(IsRotationalTest, DeviceNotUnderDev) {
  EXPECT_FALSE(clobber_wipe_.IsRotational(fake_sys_.GetPath().Append("sdc6")));
}

TEST_F(IsRotationalTest, NoRotationalFile) {
  std::string device_name = "sdq5";
  std::string disk_name = "sdq";
  base::FilePath device = fake_dev_.GetPath().Append(device_name);
  base::FilePath disk = fake_dev_.GetPath().Append(disk_name);
  ASSERT_TRUE(CreateDirectoryAndWriteFile(device, ""));
  ASSERT_TRUE(CreateDirectoryAndWriteFile(disk, ""));

  struct stat st;
  st.st_rdev = makedev(14, 7);
  st.st_mode = S_IFBLK;
  clobber_wipe_.SetStatResultForPath(device, st);

  st.st_rdev = makedev(14, 0);
  clobber_wipe_.SetStatResultForPath(disk, st);

  EXPECT_FALSE(clobber_wipe_.IsRotational(device));
}

TEST_F(IsRotationalTest, NoMatchingBaseDevice) {
  std::string device_name = "mmcblk1p5";
  std::string disk_name = "sda";
  base::FilePath device = fake_dev_.GetPath().Append(device_name);
  base::FilePath disk = fake_dev_.GetPath().Append(disk_name);
  ASSERT_TRUE(CreateDirectoryAndWriteFile(device, ""));
  ASSERT_TRUE(CreateDirectoryAndWriteFile(disk, ""));

  struct stat st;
  st.st_rdev = makedev(5, 3);
  st.st_mode = S_IFBLK;
  clobber_wipe_.SetStatResultForPath(device, st);

  st.st_rdev = makedev(7, 0);
  clobber_wipe_.SetStatResultForPath(disk, st);

  base::FilePath rotational_file =
      fake_sys_.GetPath().Append("block").Append(disk_name).Append(
          "queue/rotational");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(rotational_file, "1\n"));
  EXPECT_FALSE(clobber_wipe_.IsRotational(device));
}

TEST_F(IsRotationalTest, DifferentRotationalFileFormats) {
  std::string device_name = "mmcblk1p5";
  std::string disk_name = "mmcblk1";
  base::FilePath device = fake_dev_.GetPath().Append(device_name);
  base::FilePath disk = fake_dev_.GetPath().Append(disk_name);
  ASSERT_TRUE(CreateDirectoryAndWriteFile(device, ""));
  ASSERT_TRUE(CreateDirectoryAndWriteFile(disk, ""));

  struct stat st;
  st.st_rdev = makedev(5, 3);
  st.st_mode = S_IFBLK;
  clobber_wipe_.SetStatResultForPath(device, st);

  st.st_rdev = makedev(5, 0);
  clobber_wipe_.SetStatResultForPath(disk, st);

  base::FilePath rotational_file =
      fake_sys_.GetPath().Append("block").Append(disk_name).Append(
          "queue/rotational");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(rotational_file, "0\n"));
  EXPECT_FALSE(clobber_wipe_.IsRotational(device));

  ASSERT_TRUE(CreateDirectoryAndWriteFile(rotational_file, "0"));
  EXPECT_FALSE(clobber_wipe_.IsRotational(device));

  ASSERT_TRUE(CreateDirectoryAndWriteFile(rotational_file, "aldf"));
  EXPECT_FALSE(clobber_wipe_.IsRotational(device));

  ASSERT_TRUE(CreateDirectoryAndWriteFile(rotational_file, "1"));
  EXPECT_TRUE(clobber_wipe_.IsRotational(device));

  ASSERT_TRUE(CreateDirectoryAndWriteFile(rotational_file, "1\n"));
  EXPECT_TRUE(clobber_wipe_.IsRotational(device));
}

TEST_F(IsRotationalTest, MultipleDevices) {
  std::string device_name_one = "mmcblk1p5";
  std::string disk_name_one = "mmcblk1";
  std::string device_name_two = "nvme2n1p1";
  std::string disk_name_two = "nvme2n1";
  base::FilePath device_one = fake_dev_.GetPath().Append(device_name_one);
  base::FilePath disk_one = fake_dev_.GetPath().Append(disk_name_one);
  base::FilePath device_two = fake_dev_.GetPath().Append(device_name_two);
  base::FilePath disk_two = fake_dev_.GetPath().Append(disk_name_two);
  ASSERT_TRUE(CreateDirectoryAndWriteFile(device_one, ""));
  ASSERT_TRUE(CreateDirectoryAndWriteFile(disk_one, ""));
  ASSERT_TRUE(CreateDirectoryAndWriteFile(device_two, ""));
  ASSERT_TRUE(CreateDirectoryAndWriteFile(disk_two, ""));

  struct stat st;
  st.st_rdev = makedev(5, 5);
  st.st_mode = S_IFBLK;
  clobber_wipe_.SetStatResultForPath(device_one, st);

  st.st_rdev = makedev(5, 0);
  clobber_wipe_.SetStatResultForPath(disk_one, st);

  st.st_rdev = makedev(2, 1);
  st.st_mode = S_IFBLK;
  clobber_wipe_.SetStatResultForPath(device_two, st);

  st.st_rdev = makedev(2, 0);
  clobber_wipe_.SetStatResultForPath(disk_two, st);

  base::FilePath rotational_file_one = fake_sys_.GetPath()
                                           .Append("block")
                                           .Append(disk_name_one)
                                           .Append("queue/rotational");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(rotational_file_one, "0\n"));

  base::FilePath rotational_file_two = fake_sys_.GetPath()
                                           .Append("block")
                                           .Append(disk_name_two)
                                           .Append("queue/rotational");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(rotational_file_two, "1"));

  EXPECT_FALSE(clobber_wipe_.IsRotational(device_one));
  EXPECT_TRUE(clobber_wipe_.IsRotational(device_two));
}
