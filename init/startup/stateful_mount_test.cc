// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <time.h>

#include <deque>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/values.h>
#include <brillo/blkdev_utils/mock_lvm.h>
#include <brillo/blkdev_utils/lvm.h>
#include <gtest/gtest.h>

#include "init/crossystem.h"
#include "init/crossystem_fake.h"
#include "init/startup/chromeos_startup.h"
#include "init/startup/fake_platform_impl.h"
#include "init/startup/mock_platform_impl.h"
#include "init/startup/platform_impl.h"

using testing::_;
using testing::ByMove;
using testing::Return;
using testing::StrictMock;

namespace {

const char kImageVarsContent[] =
    R"({"load_base_vars": {"FORMAT_STATE": "base", "PLATFORM_FORMAT_STATE": )"
    R"("ext4", "PLATFORM_OPTIONS_STATE": "", "PARTITION_NUM_STATE": 1},)"
    R"("load_partition_vars": {"FORMAT_STATE": "partition", )"
    R"("PLATFORM_FORMAT_STATE": "ext4", "PLATFORM_OPTIONS_STATE": "", )"
    R"("PARTITION_NUM_STATE": 1}})";

constexpr char kDumpe2fsStr[] =
    "dumpe2fs\n%s(group "
    "android-reserved-disk)\nFilesystem features:      %s\n";

constexpr char kReservedBlocksGID[] = "Reserved blocks gid:      20119";

constexpr char kHiberResumeInitLog[] = "run/hibernate/hiber-resume-init.log";

// Helper function to create directory and write to file.
bool CreateDirAndWriteFile(const base::FilePath& path,
                           const std::string& contents) {
  return base::CreateDirectory(path.DirName()) &&
         base::WriteFile(path, contents.c_str(), contents.length()) ==
             contents.length();
}

}  // namespace

TEST(GetImageVars, BaseVars) {
  base::ScopedTempDir temp_dir_;
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  base::FilePath json_file = temp_dir_.GetPath().Append("vars.json");
  ASSERT_TRUE(WriteFile(json_file, kImageVarsContent));
  base::Value vars;
  ASSERT_TRUE(
      startup::StatefulMount::GetImageVars(json_file, "load_base_vars", &vars));
  LOG(INFO) << "vars is: " << vars;
  EXPECT_TRUE(vars.is_dict());
  const std::string* format = vars.FindStringKey("FORMAT_STATE");
  EXPECT_NE(format, nullptr);
  EXPECT_EQ(*format, "base");
}

TEST(GetImageVars, PartitionVars) {
  base::ScopedTempDir temp_dir_;
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  base::FilePath json_file = temp_dir_.GetPath().Append("vars.json");
  ASSERT_TRUE(WriteFile(json_file, kImageVarsContent));
  base::Value vars;
  ASSERT_TRUE(startup::StatefulMount::GetImageVars(
      json_file, "load_partition_vars", &vars));
  LOG(INFO) << "vars is: " << vars;
  EXPECT_TRUE(vars.is_dict());
  const std::string* format = vars.FindStringKey("FORMAT_STATE");
  LOG(INFO) << "FORMAT_STATE is: " << *format;
  EXPECT_NE(format, nullptr);
  EXPECT_EQ(*format, "partition");
}

class Ext4FeaturesTest : public ::testing::Test {
 protected:
  Ext4FeaturesTest() {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir = temp_dir_.GetPath();
    platform_ = std::make_unique<startup::FakePlatform>();
  }

  startup::Flags flags_;
  std::unique_ptr<startup::StatefulMount> stateful_mount_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir;
  std::unique_ptr<startup::FakePlatform> platform_;
};

TEST_F(Ext4FeaturesTest, Encrypt) {
  std::string state_dump =
      base::StringPrintf(kDumpe2fsStr, kReservedBlocksGID, "verity");
  startup::Flags flags;
  flags.direncryption = true;
  base::FilePath encrypt_file =
      base_dir.Append("sys/fs/ext4/features/encryption");
  ASSERT_TRUE(CreateDirAndWriteFile(encrypt_file, "1"));

  struct stat st;
  platform_->SetStatResultForPath(encrypt_file, st);

  stateful_mount_ = std::make_unique<startup::StatefulMount>(
      flags, base_dir, base_dir, platform_.get(),
      std::unique_ptr<brillo::MockLogicalVolumeManager>());
  std::vector<std::string> features =
      stateful_mount_->GenerateExt4Features(state_dump);
  std::string features_str = base::JoinString(features, " ");
  EXPECT_EQ(features_str, "-O encrypt");
}

TEST_F(Ext4FeaturesTest, Verity) {
  std::string state_dump =
      base::StringPrintf(kDumpe2fsStr, kReservedBlocksGID, "encrypt");
  startup::Flags flags;
  flags.fsverity = true;
  base::FilePath verity_file = base_dir.Append("sys/fs/ext4/features/verity");
  ASSERT_TRUE(CreateDirAndWriteFile(verity_file, "1"));

  struct stat st;
  platform_->SetStatResultForPath(verity_file, st);

  stateful_mount_ = std::make_unique<startup::StatefulMount>(
      flags, base_dir, base_dir, platform_.get(),
      std::unique_ptr<brillo::MockLogicalVolumeManager>());
  std::vector<std::string> features =
      stateful_mount_->GenerateExt4Features(state_dump);
  std::string features_str = base::JoinString(features, " ");
  EXPECT_EQ(features_str, "-O verity");
}

TEST_F(Ext4FeaturesTest, ReservedBlocksGID) {
  std::string state_dump =
      base::StringPrintf(kDumpe2fsStr, "", "encrypt verity");
  startup::Flags flags;

  stateful_mount_ = std::make_unique<startup::StatefulMount>(
      flags, base_dir, base_dir, platform_.get(),
      std::unique_ptr<brillo::MockLogicalVolumeManager>());
  std::vector<std::string> features =
      stateful_mount_->GenerateExt4Features(state_dump);
  std::string features_str = base::JoinString(features, " ");
  EXPECT_EQ(features_str, "-g 20119");
}

TEST_F(Ext4FeaturesTest, EnableQuotaWithPrjQuota) {
  std::string state_dump =
      base::StringPrintf(kDumpe2fsStr, kReservedBlocksGID, "encrypt verity");
  startup::Flags flags;
  flags.prjquota = true;
  base::FilePath quota_file = base_dir.Append("proc/sys/fs/quota");
  ASSERT_TRUE(base::CreateDirectory(quota_file));

  struct stat st;
  st.st_mode = S_IFDIR;
  platform_->SetStatResultForPath(quota_file, st);

  stateful_mount_ = std::make_unique<startup::StatefulMount>(
      flags, base_dir, base_dir, platform_.get(),
      std::unique_ptr<brillo::MockLogicalVolumeManager>());
  std::vector<std::string> features =
      stateful_mount_->GenerateExt4Features(state_dump);
  std::string features_str = base::JoinString(features, " ");
  EXPECT_EQ(features_str, "-Qusrquota,grpquota -Qprjquota -O quota");
}

TEST_F(Ext4FeaturesTest, EnableQuotaNoPrjQuota) {
  std::string state_dump = base::StringPrintf(kDumpe2fsStr, kReservedBlocksGID,
                                              "encrypt verity project");
  startup::Flags flags;
  flags.prjquota = false;
  base::FilePath quota_file = base_dir.Append("proc/sys/fs/quota");
  ASSERT_TRUE(base::CreateDirectory(quota_file));

  struct stat st;
  st.st_mode = S_IFDIR;
  platform_->SetStatResultForPath(quota_file, st);

  stateful_mount_ = std::make_unique<startup::StatefulMount>(
      flags, base_dir, base_dir, platform_.get(),
      std::unique_ptr<brillo::MockLogicalVolumeManager>());
  std::vector<std::string> features =
      stateful_mount_->GenerateExt4Features(state_dump);
  std::string features_str = base::JoinString(features, " ");
  EXPECT_EQ(features_str, "-Qusrquota,grpquota -Q^prjquota -O quota");
}

TEST_F(Ext4FeaturesTest, DisableQuota) {
  std::string state_dump = base::StringPrintf(kDumpe2fsStr, kReservedBlocksGID,
                                              "encrypt verityquota");
  startup::Flags flags;

  stateful_mount_ = std::make_unique<startup::StatefulMount>(
      flags, base_dir, base_dir, platform_.get(),
      std::unique_ptr<brillo::MockLogicalVolumeManager>());
  std::vector<std::string> features =
      stateful_mount_->GenerateExt4Features(state_dump);
  std::string features_str = base::JoinString(features, " ");
  EXPECT_EQ(features_str, "-Q^usrquota,^grpquota,^prjquota -O ^quota");
}

TEST_F(Ext4FeaturesTest, MissingFeatures) {
  std::string state_dump("");
  startup::Flags flags;

  stateful_mount_ = std::make_unique<startup::StatefulMount>(
      flags, base_dir, base_dir, platform_.get(),
      std::unique_ptr<brillo::MockLogicalVolumeManager>());
  std::vector<std::string> features =
      stateful_mount_->GenerateExt4Features(state_dump);
  std::string features_str = base::JoinString(features, " ");
  EXPECT_EQ(features_str, "-g 20119");
}

class HibernateResumeBootTest : public ::testing::Test {
 protected:
  HibernateResumeBootTest() {}

  void SetUp() override {
    cros_system_ = std::make_unique<CrosSystemFake>();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    mock_platform_ = std::make_unique<StrictMock<startup::MockPlatform>>();
    stateful_mount_ = std::make_unique<startup::StatefulMount>(
        flags_, base_dir_, base_dir_, mock_platform_.get(),
        std::unique_ptr<brillo::MockLogicalVolumeManager>());
    state_dev_ = base::FilePath("test");
    hiber_init_log_ = base_dir_.Append(kHiberResumeInitLog);
  }

  std::unique_ptr<CrosSystemFake> cros_system_;
  startup::Flags flags_;
  std::unique_ptr<startup::MockPlatform> mock_platform_;
  std::unique_ptr<startup::StatefulMount> stateful_mount_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  base::FilePath state_dev_;
  base::FilePath hiber_init_log_;
};

TEST_F(HibernateResumeBootTest, NoHibermanFile) {
  stateful_mount_->SetStateDevForTest(state_dev_);
  EXPECT_FALSE(stateful_mount_->HibernateResumeBoot());
}

TEST_F(HibernateResumeBootTest, HibermanFail) {
  stateful_mount_->SetStateDevForTest(state_dev_);
  base::FilePath hiberman = base_dir_.Append("sbin/hiberman");
  ASSERT_TRUE(CreateDirAndWriteFile(hiberman, "1"));

  EXPECT_CALL(*mock_platform_, RunHiberman(hiber_init_log_))
      .WillOnce(Return(false));

  EXPECT_FALSE(stateful_mount_->HibernateResumeBoot());
}

TEST_F(HibernateResumeBootTest, HibermanSuccess) {
  stateful_mount_->SetStateDevForTest(state_dev_);
  base::FilePath hiberman = base_dir_.Append("sbin/hiberman");
  ASSERT_TRUE(CreateDirAndWriteFile(hiberman, "1"));

  EXPECT_CALL(*mock_platform_, RunHiberman(hiber_init_log_))
      .WillOnce(Return(true));

  EXPECT_TRUE(stateful_mount_->HibernateResumeBoot());
}
