// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/stateful_mount.h"

#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <time.h>

#include <deque>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/values.h>
#include <brillo/file_utils.h>
#include <gtest/gtest.h>
#include <libstorage/platform/fake_platform.h>
#include <libstorage/platform/mock_platform.h>

#include "init/libpreservation/fake_ext2fs.h"
#include "init/libpreservation/file_preseeder.h"
#include "init/libpreservation/filesystem_manager.h"
#include "init/startup/fake_startup_dep_impl.h"
#include "init/startup/standard_mount_helper.h"
#include "init/startup/startup_dep_impl.h"

using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::Return;
using testing::StrictMock;

namespace {

constexpr char kStatefulPartition[] = "mnt/stateful_partition";

}  // namespace

class Ext4FeaturesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    platform_ = std::make_unique<libstorage::FakePlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
  }

  startup::Flags flags_{};
  std::unique_ptr<startup::StatefulMount> stateful_mount_;
  base::FilePath base_dir{"/"};
  std::unique_ptr<libstorage::FakePlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
};

TEST_F(Ext4FeaturesTest, Encrypt) {
  flags_.direncryption = true;
  base::FilePath encrypt_file =
      base_dir.Append("sys/fs/ext4/features/encryption");
  ASSERT_TRUE(platform_->WriteStringToFile(encrypt_file, "1"));

  stateful_mount_ = std::make_unique<startup::StatefulMount>(
      base_dir, base_dir, platform_.get(), startup_dep_.get());
  std::vector<std::string> features =
      stateful_mount_->GenerateExt4Features(&flags_);
  std::string features_str = base::JoinString(features, " ");
  EXPECT_EQ(features_str,
            "-g 20119 -Qusrquota,grpquota -Q^prjquota -O encrypt,quota");
}

TEST_F(Ext4FeaturesTest, Verity) {
  flags_.fsverity = true;
  base::FilePath verity_file = base_dir.Append("sys/fs/ext4/features/verity");
  ASSERT_TRUE(platform_->WriteStringToFile(verity_file, "1"));

  stateful_mount_ = std::make_unique<startup::StatefulMount>(
      base_dir, base_dir, platform_.get(), startup_dep_.get());
  std::vector<std::string> features =
      stateful_mount_->GenerateExt4Features(&flags_);
  std::string features_str = base::JoinString(features, " ");
  EXPECT_EQ(features_str,
            "-g 20119 -Qusrquota,grpquota -Q^prjquota -O verity,quota");
}

TEST_F(Ext4FeaturesTest, ReservedBlocksGID) {
  stateful_mount_ = std::make_unique<startup::StatefulMount>(
      base_dir, base_dir, platform_.get(), startup_dep_.get());
  std::vector<std::string> features =
      stateful_mount_->GenerateExt4Features(&flags_);
  std::string features_str = base::JoinString(features, " ");
  EXPECT_EQ(features_str, "-g 20119 -Qusrquota,grpquota -Q^prjquota -O quota");
}

TEST_F(Ext4FeaturesTest, EnableQuotaWithPrjQuota) {
  flags_.prjquota = true;

  stateful_mount_ = std::make_unique<startup::StatefulMount>(
      base_dir, base_dir, platform_.get(), startup_dep_.get());
  std::vector<std::string> features =
      stateful_mount_->GenerateExt4Features(&flags_);
  std::string features_str = base::JoinString(features, " ");
  EXPECT_EQ(features_str, "-g 20119 -Qusrquota,grpquota -Qprjquota -O quota");
}

TEST_F(Ext4FeaturesTest, EnableQuotaNoPrjQuota) {
  stateful_mount_ = std::make_unique<startup::StatefulMount>(
      base_dir, base_dir, platform_.get(), startup_dep_.get());
  std::vector<std::string> features =
      stateful_mount_->GenerateExt4Features(&flags_);
  std::string features_str = base::JoinString(features, " ");
  EXPECT_EQ(features_str, "-g 20119 -Qusrquota,grpquota -Q^prjquota -O quota");
}

class DevUpdateStatefulTest : public ::testing::Test {
 protected:
  void SetUp() override {
    stateful = base_dir.Append(kStatefulPartition);
    platform_ = std::make_unique<libstorage::FakePlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    stateful_update_file = stateful.Append(".update_available");
    var_new = stateful.Append("var_new");
    var_target = stateful.Append("var_overlay");
    developer_target = stateful.Append("dev_image");
    developer_new = stateful.Append("dev_image_new");
    dev_image_target = stateful.Append("unencrypted/dev_image.block");
    dev_image_new = stateful.Append("unencrypted/dev_image_new.block");
    preserve_dir = stateful.Append("unencrypted/preserve");
    stateful_mount_ = std::make_unique<startup::StatefulMount>(
        base_dir, stateful, platform_.get(), startup_dep_.get());
  }

  base::FilePath base_dir{"/"};
  base::FilePath stateful;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  startup::Flags flags_{};
  std::unique_ptr<libstorage::FakePlatform> platform_;
  std::unique_ptr<startup::StatefulMount> stateful_mount_;
  base::FilePath stateful_update_file;
  base::FilePath var_new;
  base::FilePath var_target;
  base::FilePath developer_target;
  base::FilePath developer_new;
  base::FilePath dev_image_new;
  base::FilePath dev_image_target;
  base::FilePath preserve_dir;
};

TEST_F(DevUpdateStatefulTest, NoUpdateAvailable) {
  EXPECT_TRUE(stateful_mount_->DevUpdateStatefulPartition("", false));
}

TEST_F(DevUpdateStatefulTest, NewDevAndVarNoClobber) {
  ASSERT_TRUE(platform_->CreateDirectory(developer_new));
  ASSERT_TRUE(platform_->CreateDirectory(var_new));

  ASSERT_TRUE(platform_->WriteStringToFile(stateful_update_file, "1"));

  LOG(INFO) << "var new test: " << var_new.value();
  LOG(INFO) << "developer_new test: " << developer_new.value();

  ASSERT_TRUE(
      platform_->WriteStringToFile(developer_new.Append("dev_new_file"), "1"));
  ASSERT_TRUE(
      platform_->WriteStringToFile(var_new.Append("var_new_file"), "1"));
  ASSERT_TRUE(platform_->WriteStringToFile(
      developer_target.Append("dev_target_file"), "1"));
  ASSERT_TRUE(
      platform_->WriteStringToFile(var_target.Append("var_target_file"), "1"));

  EXPECT_TRUE(stateful_mount_->DevUpdateStatefulPartition("", false));

  EXPECT_FALSE(platform_->FileExists(developer_new.Append("dev_new_file")));
  EXPECT_FALSE(platform_->FileExists(var_new.Append("var_new_file")));
  EXPECT_FALSE(
      platform_->FileExists(developer_target.Append("dev_target_file")));
  EXPECT_FALSE(platform_->FileExists(var_target.Append("var_target_file")));

  EXPECT_FALSE(platform_->FileExists(stateful_update_file));
  EXPECT_TRUE(platform_->FileExists(var_target.Append("var_new_file")));
  EXPECT_TRUE(platform_->FileExists(developer_target.Append("dev_new_file")));

  std::string message =
      "Updating from " + developer_new.value() + " && " + var_new.value() + ".";
  std::string res;
  startup_dep_->GetClobberLog(&res);
  EXPECT_EQ(res, message);
}

TEST_F(DevUpdateStatefulTest, NewDevImageNoClobber) {
  ASSERT_TRUE(platform_->CreateDirectory(stateful.Append("unencrypted")));
  ASSERT_TRUE(platform_->TouchFileDurable(dev_image_new));

  ASSERT_TRUE(platform_->WriteStringToFile(stateful_update_file, "1"));

  LOG(INFO) << "dev_image_new test: " << dev_image_new.value();

  ASSERT_TRUE(platform_->WriteStringToFile(dev_image_new, "123"));

  EXPECT_TRUE(stateful_mount_->DevUpdateStatefulPartition("", false));

  EXPECT_FALSE(platform_->FileExists(dev_image_new));

  EXPECT_FALSE(platform_->FileExists(stateful_update_file));
  EXPECT_TRUE(platform_->FileExists(dev_image_target));

  std::string message =
      "Updating from " + developer_new.value() + " && " + var_new.value() + ".";
  std::string res;
  startup_dep_->GetClobberLog(&res);
  EXPECT_EQ(res, message);
}

TEST_F(DevUpdateStatefulTest, NoNewDevAndVarWithClobber) {
  ASSERT_TRUE(platform_->WriteStringToFile(stateful_update_file, "clobber"));
  base::FilePath labmachine = stateful.Append(".labmachine");
  base::FilePath encrypted_key = stateful.Append("encrypted.key");
  base::FilePath encrypted_block = stateful.Append("encrypted.block");
  base::FilePath test_dir = stateful.Append("test");
  base::FilePath test = test_dir.Append("test");
  base::FilePath preserve_test = preserve_dir.Append("test");
  base::FilePath empty = stateful.Append("empty");

  ASSERT_TRUE(platform_->CreateDirectory(stateful.Append("unencrypted")));
  ASSERT_TRUE(platform_->WriteStringToFile(dev_image_target, "1"));
  ASSERT_TRUE(platform_->CreateDirectory(empty));
  ASSERT_TRUE(platform_->CreateDirectory(test_dir));
  ASSERT_TRUE(platform_->WriteStringToFile(
      developer_target.Append("dev_target_file"), "1"));
  ASSERT_TRUE(
      platform_->WriteStringToFile(var_target.Append("var_target_file"), "1"));
  ASSERT_TRUE(platform_->WriteStringToFile(labmachine, "1"));
  ASSERT_TRUE(platform_->WriteStringToFile(encrypted_key, "1"));
  ASSERT_TRUE(platform_->WriteStringToFile(encrypted_block, "1"));
  ASSERT_TRUE(platform_->WriteStringToFile(test, "1"));
  ASSERT_TRUE(platform_->WriteStringToFile(preserve_test, "1"));

  EXPECT_TRUE(stateful_mount_->DevUpdateStatefulPartition("", false));
  EXPECT_TRUE(
      platform_->FileExists(developer_target.Append("dev_target_file")));
  EXPECT_TRUE(platform_->FileExists(var_target.Append("var_target_file")));
  EXPECT_TRUE(platform_->FileExists(labmachine));
  EXPECT_TRUE(platform_->FileExists(encrypted_key));
  EXPECT_TRUE(platform_->FileExists(encrypted_block));
  EXPECT_FALSE(platform_->DirectoryExists(test_dir));
  EXPECT_TRUE(platform_->FileExists(preserve_test));
  EXPECT_TRUE(platform_->FileExists(dev_image_target));
  EXPECT_FALSE(platform_->FileExists(empty));

  std::string message = "Stateful update did not find " +
                        developer_new.value() + " & " + var_new.value() +
                        ".'\n'Keeping old development tools.";
  std::string res;
  startup_dep_->GetClobberLog(&res);
  EXPECT_EQ(res, message);
}

TEST_F(DevUpdateStatefulTest, PreserveDirectory) {
  base::FilePath wipe = stateful.Append("wipe");
  base::FilePath wipe_subdir = wipe.Append("wipe_subdir");
  base::FilePath not_empty = wipe.Append("wipe_not_empty");
  base::FilePath not_empty_file = not_empty.Append("test");
  base::FilePath preserve = stateful.Append("preserve");
  base::FilePath preserve_subdir = preserve.Append("preserve_subdir");

  ASSERT_TRUE(platform_->CreateDirectory(wipe));
  ASSERT_TRUE(platform_->CreateDirectory(wipe_subdir));
  ASSERT_TRUE(platform_->CreateDirectory(not_empty));
  ASSERT_TRUE(platform_->WriteStringToFile(not_empty_file, "1"));
  ASSERT_TRUE(platform_->CreateDirectory(preserve));
  ASSERT_TRUE(platform_->CreateDirectory(preserve_subdir));

  stateful_mount_->RemoveEmptyDirectory({preserve}, stateful);
  EXPECT_TRUE(platform_->DirectoryExists(wipe));
  EXPECT_FALSE(platform_->DirectoryExists(wipe_subdir));
  EXPECT_TRUE(platform_->DirectoryExists(not_empty));
  EXPECT_TRUE(platform_->DirectoryExists(preserve));
  EXPECT_TRUE(platform_->DirectoryExists(preserve_subdir));
}

class DevGatherLogsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    stateful = base_dir.Append(kStatefulPartition);
    platform_ = std::make_unique<libstorage::FakePlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    stateful_mount_ = std::make_unique<startup::StatefulMount>(
        base_dir, stateful, platform_.get(), startup_dep_.get());
    lab_preserve_logs_ = stateful.Append(".gatherme");
    prior_log_dir_ = stateful.Append("unencrypted/prior_logs");
    var_dir_ = base_dir.Append("var");
    home_chronos_ = base_dir.Append("home/chronos");
    ASSERT_TRUE(platform_->CreateDirectory(prior_log_dir_));
    ASSERT_TRUE(platform_->CreateDirectory(var_dir_));
    ASSERT_TRUE(platform_->CreateDirectory(home_chronos_));
  }

  base::FilePath base_dir{"/"};
  base::FilePath stateful;
  base::FilePath lab_preserve_logs_;
  base::FilePath prior_log_dir_;
  base::FilePath var_dir_;
  base::FilePath home_chronos_;
  std::unique_ptr<libstorage::FakePlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  startup::Flags flags_{};
  std::unique_ptr<startup::StatefulMount> stateful_mount_;
};

TEST_F(DevGatherLogsTest, NoPreserveLogs) {
  ASSERT_TRUE(platform_->WriteStringToFile(lab_preserve_logs_, "#"));
  stateful_mount_->DevGatherLogs(base_dir);
}

TEST_F(DevGatherLogsTest, PreserveLogs) {
  base::FilePath test = base_dir.Append("test");
  base::FilePath test1 = test.Append("test1");
  base::FilePath test2 = test.Append("test2");
  base::FilePath standalone = base_dir.Append("parent/standalone");
  base::FilePath var_logs = base_dir.Append("var/logs");
  base::FilePath log1 = var_logs.Append("log1");
  base::FilePath home_chronos = base_dir.Append("home/chronos/test");

  base::FilePath prior_test = prior_log_dir_.Append("test");
  base::FilePath prior_test1 = prior_test.Append("test1");
  base::FilePath prior_test2 = prior_test.Append("test2");
  base::FilePath prior_standalone = prior_log_dir_.Append("standalone");
  base::FilePath prior_log1 = prior_log_dir_.Append("logs/log1");

  std::string preserve_str("#\n");
  preserve_str.append(test.value());
  preserve_str.append("\n");
  preserve_str.append(standalone.value());
  preserve_str.append("\n#ignore\n\n");
  preserve_str.append(var_logs.value());

  ASSERT_TRUE(platform_->WriteStringToFile(lab_preserve_logs_, preserve_str));
  ASSERT_TRUE(platform_->WriteStringToFile(test1, "#"));
  ASSERT_TRUE(platform_->WriteStringToFile(test2, "#"));
  ASSERT_TRUE(platform_->WriteStringToFile(standalone, "#"));
  ASSERT_TRUE(platform_->WriteStringToFile(log1, "#"));
  ASSERT_TRUE(platform_->WriteStringToFile(home_chronos, "#"));

  EXPECT_TRUE(platform_->FileExists(home_chronos));

  stateful_mount_->DevGatherLogs(base_dir);

  EXPECT_TRUE(platform_->FileExists(prior_test1));
  EXPECT_TRUE(platform_->FileExists(prior_test2));
  EXPECT_TRUE(platform_->FileExists(prior_standalone));
  EXPECT_TRUE(platform_->FileExists(prior_log1));
  EXPECT_TRUE(platform_->FileExists(standalone));
  EXPECT_FALSE(platform_->FileExists(lab_preserve_logs_));
}
#if USE_LVM_STATEFUL_PARTITION
class RunMountStatefulLVM : public ::testing::Test {
 protected:
  RunMountStatefulLVM() {}

  void SetUp() override {
    platform_ = std::make_unique<libstorage::MockPlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    storage_factory_ = std::make_unique<libstorage::StorageContainerFactory>(
        platform_.get(), nullptr);
    mount_helper_ = std::make_unique<startup::StandardMountHelper>(
        platform_.get(), startup_dep_.get(), &flags_, base_dir,
        std::unique_ptr<startup::MountVarAndHomeChronosInterface>(),
        storage_factory_.get());
    flags_.lvm_stateful = true;
    stateful_mount_ = std::make_unique<startup::StatefulMount>(
        base_dir, stateful_dir, platform_.get(), startup_dep_.get());

    // Setup a default partition information.
    std::string vars_content = R"""(
{
   "PARTITION_NUM_STATE": "1",
   "FS_FORMAT_STATE": "ext4"
})""";

    partition_info_ = base::JSONReader::ReadAndReturnValueWithError(
        vars_content, base::JSON_PARSE_RFC);
    ASSERT_TRUE(partition_info_.has_value());
    ASSERT_TRUE(partition_info_->is_dict());
  }

  startup::Flags flags_;
  std::unique_ptr<startup::StatefulMount> stateful_mount_;
  base::FilePath base_dir{"/"};
  base::FilePath stateful_dir{"/state"};
  std::unique_ptr<libstorage::MockPlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  std::unique_ptr<libstorage::StorageContainerFactory> storage_factory_;
  std::unique_ptr<startup::StandardMountHelper> mount_helper_;
  base::JSONReader::Result partition_info_;
};

TEST_F(RunMountStatefulLVM, NoStatefulPartition) {
  // No stateful partition, full clobber.
  base::FilePath rootdev{"/dev/mmc0blk1"};
  EXPECT_CALL(*platform_, Fsck(_, _, _)).Times(0);
  EXPECT_CALL(*platform_, Mount(_, _, _, _, _)).Times(0);
  stateful_mount_->MountStateful(rootdev, &flags_, mount_helper_.get(),
                                 *partition_info_, std::nullopt);

  std::set<std::string> expected = {"fast", "keepimg", "preserve_lvs"};
  EXPECT_EQ(startup_dep_->GetClobberArgs(), expected);
}

TEST_F(RunMountStatefulLVM, StatefulPartitionEmpty) {
  base::FilePath rootdev{"/dev/mmc0blk1"};
  base::FilePath statefuldev{"/dev/mmc0blk1p1"};
  ASSERT_TRUE(platform_->WriteStringToFile(statefuldev, ""));
  base::stat_wrapper_t st;
  st.st_mode = S_IFBLK;
  EXPECT_CALL(*platform_, Stat(statefuldev, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(st), Return(true)));
  EXPECT_CALL(*platform_, Fsck(statefuldev, _, _)).WillOnce(Return(false));
  EXPECT_CALL(*platform_, Tune2Fs(statefuldev, _)).WillOnce(Return(false));
  EXPECT_CALL(*platform_, Mount(_, _, _, _, _)).Times(0);

  stateful_mount_->MountStateful(rootdev, &flags_, mount_helper_.get(),
                                 *partition_info_, std::nullopt);

  std::set<std::string> expected = {"fast", "keepimg", "preserve_lvs"};
  EXPECT_EQ(startup_dep_->GetClobberArgs(), expected);
}
#endif  // USE_LVM_STATEFUL_PARTITION

class RunMountStateful : public ::testing::Test {
 protected:
  RunMountStateful() {}

  void SetUp() override {
    platform_ = std::make_unique<libstorage::MockPlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    storage_factory_ = std::make_unique<libstorage::StorageContainerFactory>(
        platform_.get(), nullptr);
    mount_helper_ = std::make_unique<startup::StandardMountHelper>(
        platform_.get(), startup_dep_.get(), &flags_, base_dir,
        std::unique_ptr<startup::MountVarAndHomeChronosInterface>(),
        storage_factory_.get());
    stateful_mount_ = std::make_unique<startup::StatefulMount>(
        base_dir, stateful_dir, platform_.get(), startup_dep_.get());

    // Setup a default partition information.
    std::string vars_content = R"""(
{
   "PARTITION_NUM_STATE": "1",
   "FS_FORMAT_STATE": "ext4"
})""";

    partition_info_ = base::JSONReader::ReadAndReturnValueWithError(
        vars_content, base::JSON_PARSE_RFC);
    ASSERT_TRUE(partition_info_.has_value());
    ASSERT_TRUE(partition_info_->is_dict());
  }

  startup::Flags flags_;
  std::unique_ptr<startup::StatefulMount> stateful_mount_;
  base::FilePath base_dir{"/"};
  base::FilePath stateful_dir{"/state"};
  std::unique_ptr<libstorage::MockPlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  std::unique_ptr<libstorage::StorageContainerFactory> storage_factory_;
  std::unique_ptr<startup::StandardMountHelper> mount_helper_;
  base::JSONReader::Result partition_info_;
};

TEST_F(RunMountStateful, NoStatefulPartition) {
  // No stateful partition, full clobber.
  base::FilePath rootdev{"/dev/mmc0blk1"};
  EXPECT_CALL(*platform_, Fsck(_, _, _)).Times(0);
  EXPECT_CALL(*platform_, Mount(_, _, _, _, _)).Times(0);
  stateful_mount_->MountStateful(rootdev, &flags_, mount_helper_.get(),
                                 *partition_info_, std::nullopt);

  std::set<std::string> expected = {"fast", "keepimg", "preserve_lvs"};
  EXPECT_EQ(startup_dep_->GetClobberArgs(), expected);
}

TEST_F(RunMountStateful, StatefulPartitionEmpty) {
  base::FilePath rootdev{"/dev/mmc0blk1"};
  base::FilePath statefuldev{"/dev/mmc0blk1p1"};
  ASSERT_TRUE(platform_->WriteStringToFile(statefuldev, ""));
  base::stat_wrapper_t st;
  st.st_mode = S_IFBLK;
  EXPECT_CALL(*platform_, Stat(statefuldev, _))
      .WillRepeatedly(DoAll(SetArgPointee<1>(st), Return(true)));
  EXPECT_CALL(*platform_, Fsck(statefuldev, _, _)).WillOnce(Return(false));
  EXPECT_CALL(*platform_, Tune2Fs(statefuldev, _)).WillOnce(Return(false));
  EXPECT_CALL(*platform_, Mount(_, _, _, _, _)).Times(0);

  stateful_mount_->MountStateful(rootdev, &flags_, mount_helper_.get(),
                                 *partition_info_, std::nullopt);

  std::set<std::string> expected = {"fast", "keepimg", "preserve_lvs"};
  EXPECT_EQ(startup_dep_->GetClobberArgs(), expected);
}

TEST(MountStateful, PreserveBootFiles) {
  // Create tempdir
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());

  base::FilePath metadata_path = temp_dir.GetPath().Append("preseeder.proto");

  auto fs = libpreservation::FakeExt2fs::Create(base::FilePath("/dev/null"));
  libpreservation::FilesystemManager fs_manager(std::move(fs));
  libpreservation::FilePreseeder preseeder({base::FilePath("unencrypted")},
                                           base::FilePath("/"),
                                           temp_dir.GetPath(), metadata_path);

  EXPECT_TRUE(preseeder.CheckAllowlist(base::FilePath("unencrypted/rma/data")));
  EXPECT_TRUE(preseeder.CheckAllowlist(
      base::FilePath("unencrypted/preserve/clobber.log")));
  EXPECT_FALSE(preseeder.CheckAllowlist(base::FilePath("encryption.key")));
}
