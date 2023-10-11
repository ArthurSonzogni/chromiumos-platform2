// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/clobber/clobber_state.h"

#include <limits.h>
#include <stdlib.h>
#include <sys/sysmacros.h>

#include <memory>
#include <set>
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

#include "init/clobber/clobber_wipe_mock.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::StrictMock;

TEST(ParseArgv, EmptyArgs) {
  std::vector<const char*> argv{"clobber-state"};
  ClobberState::Arguments args = ClobberState::ParseArgv(argv.size(), &argv[0]);
  EXPECT_FALSE(args.factory_wipe);
  EXPECT_FALSE(args.fast_wipe);
  EXPECT_FALSE(args.keepimg);
  EXPECT_FALSE(args.safe_wipe);
  EXPECT_FALSE(args.rollback_wipe);
  EXPECT_FALSE(args.preserve_lvs);
}

TEST(ParseArgv, AllArgsIndividual) {
  std::vector<const char*> argv{"clobber-state", "fast",     "factory",
                                "keepimg",       "rollback", "safe"};
  ClobberState::Arguments args = ClobberState::ParseArgv(argv.size(), &argv[0]);
  EXPECT_TRUE(args.factory_wipe);
  EXPECT_TRUE(args.fast_wipe);
  EXPECT_TRUE(args.keepimg);
  EXPECT_TRUE(args.safe_wipe);
  EXPECT_TRUE(args.rollback_wipe);
  EXPECT_FALSE(args.preserve_lvs);
}

TEST(ParseArgv, AllArgsSquished) {
  std::vector<const char*> argv{"clobber-state",
                                "fast factory keepimg rollback safe"};
  ClobberState::Arguments args = ClobberState::ParseArgv(argv.size(), &argv[0]);
  EXPECT_TRUE(args.factory_wipe);
  EXPECT_TRUE(args.fast_wipe);
  EXPECT_TRUE(args.keepimg);
  EXPECT_TRUE(args.safe_wipe);
  EXPECT_TRUE(args.rollback_wipe);
  EXPECT_FALSE(args.preserve_lvs);
}

TEST(ParseArgv, SomeArgsIndividual) {
  std::vector<const char*> argv{"clobber-state", "rollback", "fast", "keepimg"};
  ClobberState::Arguments args = ClobberState::ParseArgv(argv.size(), &argv[0]);
  EXPECT_FALSE(args.factory_wipe);
  EXPECT_TRUE(args.fast_wipe);
  EXPECT_TRUE(args.keepimg);
  EXPECT_FALSE(args.safe_wipe);
  EXPECT_TRUE(args.rollback_wipe);
  EXPECT_FALSE(args.preserve_lvs);
}

TEST(ParseArgv, SomeArgsSquished) {
  std::vector<const char*> argv{"clobber-state", "rollback safe fast"};
  ClobberState::Arguments args = ClobberState::ParseArgv(argv.size(), &argv[0]);
  EXPECT_FALSE(args.factory_wipe);
  EXPECT_TRUE(args.fast_wipe);
  EXPECT_FALSE(args.keepimg);
  EXPECT_TRUE(args.safe_wipe);
  EXPECT_TRUE(args.rollback_wipe);
  EXPECT_FALSE(args.preserve_lvs);
}

TEST(ParseArgv, PreserveLogicalVolumesWipe) {
  {
    std::vector<const char*> argv{"clobber-state", "preserve_lvs"};
    ClobberState::Arguments args =
        ClobberState::ParseArgv(argv.size(), &argv[0]);
    EXPECT_FALSE(args.safe_wipe);
    EXPECT_EQ(args.preserve_lvs, USE_LVM_STATEFUL_PARTITION);
  }
  {
    std::vector<const char*> argv{"clobber-state", "safe preserve_lvs"};
    ClobberState::Arguments args =
        ClobberState::ParseArgv(argv.size(), &argv[0]);
    EXPECT_TRUE(args.safe_wipe);
    EXPECT_EQ(args.preserve_lvs, USE_LVM_STATEFUL_PARTITION);
  }
  {
    std::vector<const char*> argv{"clobber-state", "safe", "preserve_lvs"};
    ClobberState::Arguments args =
        ClobberState::ParseArgv(argv.size(), &argv[0]);
    EXPECT_TRUE(args.safe_wipe);
    EXPECT_EQ(args.preserve_lvs, USE_LVM_STATEFUL_PARTITION);
  }
}

TEST(IncrementFileCounter, Nonexistent) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath counter = temp_dir.GetPath().Append("counter");
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "1\n");
}

TEST(IncrementFileCounter, NegativeNumber) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath counter = temp_dir.GetPath().Append("counter");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(counter, "-3\n"));
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "1\n");
}

TEST(IncrementFileCounter, SmallNumber) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath counter = temp_dir.GetPath().Append("counter");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(counter, "42\n"));
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "43\n");
}

TEST(IncrementFileCounter, LargeNumber) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath counter = temp_dir.GetPath().Append("counter");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(counter, "1238761\n"));
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "1238762\n");
}

TEST(IncrementFileCounter, NonNumber) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath counter = temp_dir.GetPath().Append("counter");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(counter, "cruciverbalist"));
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "1\n");
}

TEST(IncrementFileCounter, IntMax) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath counter = temp_dir.GetPath().Append("counter");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(counter, std::to_string(INT_MAX)));
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "1\n");
}

TEST(IncrementFileCounter, LongMax) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath counter = temp_dir.GetPath().Append("counter");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(counter, std::to_string(LONG_MAX)));
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "1\n");
}

TEST(IncrementFileCounter, InputNoNewline) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath counter = temp_dir.GetPath().Append("counter");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(counter, std::to_string(7)));
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "8\n");
}

TEST(WriteLastPowerwashTime, FileNonexistentWriteSuccess) {
  const time_t curr_value = 55;
  base::Time parsed_time = base::Time::FromTimeT(curr_value);
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath last_powerwash_time_path =
      temp_dir.GetPath().Append("lastPowerwashTime");
  EXPECT_TRUE(ClobberState::WriteLastPowerwashTime(last_powerwash_time_path,
                                                   parsed_time));
  EXPECT_TRUE(base::PathExists(last_powerwash_time_path));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(last_powerwash_time_path, &contents));
  EXPECT_EQ(contents, "55\n");
}

TEST(WriteLastPowerwashTime, FileExistentOverwriteSuccess) {
  const time_t curr_value = 66;
  base::Time parsed_time = base::Time::FromTimeT(curr_value);
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath last_powerwash_time_path =
      temp_dir.GetPath().Append("lastPowerwashTime");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(last_powerwash_time_path, "55\n"));
  EXPECT_TRUE(ClobberState::WriteLastPowerwashTime(last_powerwash_time_path,
                                                   parsed_time));
  EXPECT_TRUE(base::PathExists(last_powerwash_time_path));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(last_powerwash_time_path, &contents));
  EXPECT_EQ(contents, "66\n");
}

TEST(PreserveFiles, NoFiles) {
  base::ScopedTempDir fake_stateful_dir;
  ASSERT_TRUE(fake_stateful_dir.CreateUniqueTempDir());
  base::FilePath fake_stateful = fake_stateful_dir.GetPath();
  ASSERT_TRUE(base::CreateDirectory(
      fake_stateful.Append("unimportant/directory/structure")));

  base::ScopedTempDir fake_tmp_dir;
  ASSERT_TRUE(fake_tmp_dir.CreateUniqueTempDir());
  base::FilePath tar_file = fake_tmp_dir.GetPath().Append("preserved.tar");

  EXPECT_EQ(ClobberState::PreserveFiles(
                fake_stateful, std::vector<base::FilePath>(), tar_file),
            0);

  EXPECT_FALSE(base::PathExists(tar_file));

  ASSERT_TRUE(CreateDirectoryAndWriteFile(tar_file, ""));
  EXPECT_TRUE(base::PathExists(tar_file));
  EXPECT_EQ(ClobberState::PreserveFiles(
                fake_stateful, std::vector<base::FilePath>(), tar_file),
            0);

  // PreserveFiles should have deleted existing tar_file.
  EXPECT_FALSE(base::PathExists(tar_file));
}

TEST(PreserveFiles, NoExistingFiles) {
  base::ScopedTempDir fake_stateful_dir;
  ASSERT_TRUE(fake_stateful_dir.CreateUniqueTempDir());
  base::FilePath fake_stateful = fake_stateful_dir.GetPath();
  ASSERT_TRUE(base::CreateDirectory(
      fake_stateful.Append("unimportant/directory/structure")));

  base::ScopedTempDir fake_tmp_dir;
  ASSERT_TRUE(fake_tmp_dir.CreateUniqueTempDir());
  base::FilePath tar_file = fake_tmp_dir.GetPath().Append("preserved.tar");
  base::FilePath nonexistent_file = fake_tmp_dir.GetPath().Append("test.txt");

  EXPECT_EQ(ClobberState::PreserveFiles(
                fake_stateful, std::vector<base::FilePath>({nonexistent_file}),
                tar_file),
            0);

  EXPECT_FALSE(base::PathExists(tar_file));

  ASSERT_TRUE(CreateDirectoryAndWriteFile(tar_file, ""));
  EXPECT_TRUE(base::PathExists(tar_file));
  EXPECT_EQ(ClobberState::PreserveFiles(
                fake_stateful, std::vector<base::FilePath>({nonexistent_file}),
                tar_file),
            0);

  // PreserveFiles should have deleted existing tar_file.
  EXPECT_FALSE(base::PathExists(tar_file));
}

TEST(PreserveFiles, OneFile) {
  base::FilePath not_preserved_file("unimportant/directory/structure/file.img");
  base::FilePath preserved_file("good/directory/file.tiff");

  base::ScopedTempDir fake_stateful_dir;
  ASSERT_TRUE(fake_stateful_dir.CreateUniqueTempDir());
  base::FilePath fake_stateful = fake_stateful_dir.GetPath();

  base::FilePath stateful_not_preserved =
      fake_stateful.Append(not_preserved_file);
  base::FilePath stateful_preserved = fake_stateful.Append(preserved_file);

  ASSERT_TRUE(CreateDirectoryAndWriteFile(stateful_not_preserved, "unneeded"));
  ASSERT_TRUE(CreateDirectoryAndWriteFile(stateful_preserved, "test_contents"));

  base::ScopedTempDir fake_tmp_dir;
  ASSERT_TRUE(fake_tmp_dir.CreateUniqueTempDir());
  base::FilePath tar_file = fake_tmp_dir.GetPath().Append("preserved.tar");

  std::vector<base::FilePath> preserved_files{preserved_file};
  EXPECT_EQ(
      ClobberState::PreserveFiles(fake_stateful, preserved_files, tar_file), 0);

  ASSERT_TRUE(base::PathExists(tar_file));

  base::ScopedTempDir expand_tar_dir;
  ASSERT_TRUE(expand_tar_dir.CreateUniqueTempDir());
  base::FilePath expand_tar_path = expand_tar_dir.GetPath();

  brillo::ProcessImpl tar;
  tar.AddArg("/bin/tar");
  tar.AddArg("-C");
  tar.AddArg(expand_tar_path.value());
  tar.AddArg("-xf");
  tar.AddArg(tar_file.value());
  ASSERT_EQ(tar.Run(), 0);

  EXPECT_FALSE(base::PathExists(expand_tar_path.Append(not_preserved_file)));

  base::FilePath expanded_preserved = expand_tar_path.Append(preserved_file);
  EXPECT_TRUE(base::PathExists(expanded_preserved));
  std::string contents;
  EXPECT_TRUE(base::ReadFileToString(expanded_preserved, &contents));
  EXPECT_EQ(contents, "test_contents");
}

TEST(PreserveFiles, ManyFiles) {
  base::FilePath not_preserved_file("unimportant/directory/structure/file.img");
  base::FilePath preserved_file_a("good/directory/file.tiff");
  base::FilePath preserved_file_b("other/folder/saved.bin");

  base::ScopedTempDir fake_stateful_dir;
  ASSERT_TRUE(fake_stateful_dir.CreateUniqueTempDir());
  base::FilePath fake_stateful = fake_stateful_dir.GetPath();

  base::FilePath stateful_not_preserved =
      fake_stateful.Append(not_preserved_file);
  base::FilePath stateful_preserved_a = fake_stateful.Append(preserved_file_a);
  base::FilePath stateful_preserved_b = fake_stateful.Append(preserved_file_b);

  ASSERT_TRUE(CreateDirectoryAndWriteFile(stateful_not_preserved, "unneeded"));
  ASSERT_TRUE(
      CreateDirectoryAndWriteFile(stateful_preserved_a, "test_contents"));
  ASSERT_TRUE(CreateDirectoryAndWriteFile(stateful_preserved_b, "data"));

  base::ScopedTempDir fake_tmp_dir;
  ASSERT_TRUE(fake_tmp_dir.CreateUniqueTempDir());
  base::FilePath tar_file = fake_tmp_dir.GetPath().Append("preserved.tar");

  std::vector<base::FilePath> preserved_files{preserved_file_a,
                                              preserved_file_b};
  EXPECT_EQ(
      ClobberState::PreserveFiles(fake_stateful, preserved_files, tar_file), 0);

  ASSERT_TRUE(base::PathExists(tar_file));

  base::ScopedTempDir expand_tar_dir;
  ASSERT_TRUE(expand_tar_dir.CreateUniqueTempDir());
  base::FilePath expand_tar_path = expand_tar_dir.GetPath();

  brillo::ProcessImpl tar;
  tar.AddArg("/bin/tar");
  tar.AddArg("-C");
  tar.AddArg(expand_tar_path.value());
  tar.AddArg("-xf");
  tar.AddArg(tar_file.value());
  ASSERT_EQ(tar.Run(), 0);

  EXPECT_FALSE(base::PathExists(expand_tar_path.Append(not_preserved_file)));

  base::FilePath expanded_preserved_a =
      expand_tar_path.Append(preserved_file_a);
  EXPECT_TRUE(base::PathExists(expanded_preserved_a));
  std::string contents_a;
  EXPECT_TRUE(base::ReadFileToString(expanded_preserved_a, &contents_a));
  EXPECT_EQ(contents_a, "test_contents");

  base::FilePath expanded_preserved_b =
      expand_tar_path.Append(preserved_file_b);
  EXPECT_TRUE(base::PathExists(expanded_preserved_b));
  std::string contents_b;
  EXPECT_TRUE(base::ReadFileToString(expanded_preserved_b, &contents_b));
  EXPECT_EQ(contents_b, "data");
}

class MarkDeveloperModeTest : public ::testing::Test {
 protected:
  MarkDeveloperModeTest()
      : crossystem_fake_(std::make_unique<crossystem::fake::CrossystemFake>()),
        cros_system_(crossystem_fake_.get()),
        clobber_ui_null_(std::make_unique<ClobberUi>(DevNull())),
        clobber_wipe_mock_(
            std::make_unique<ClobberWipeMock>(clobber_ui_null_.get())),
        clobber_(ClobberState::Arguments(),
                 std::make_unique<crossystem::Crossystem>(
                     std::move(crossystem_fake_)),
                 std::move(clobber_ui_null_),
                 std::move(clobber_wipe_mock_),
                 std::unique_ptr<ClobberLvm>()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_stateful_ = temp_dir_.GetPath();
    clobber_.SetStatefulForTest(fake_stateful_);
  }

  std::unique_ptr<crossystem::fake::CrossystemFake> crossystem_fake_;
  crossystem::fake::CrossystemFake* cros_system_;
  std::unique_ptr<ClobberUi> clobber_ui_null_;
  std::unique_ptr<ClobberWipeMock> clobber_wipe_mock_;
  ClobberState clobber_;
  base::ScopedTempDir temp_dir_;
  base::FilePath fake_stateful_;
};

TEST_F(MarkDeveloperModeTest, NotDeveloper) {
  clobber_.MarkDeveloperMode();
  EXPECT_FALSE(base::PathExists(fake_stateful_.Append(".developer_mode")));

  ASSERT_TRUE(cros_system_->VbSetSystemPropertyInt(
      crossystem::Crossystem::kDevSwitchBoot, 0));
  clobber_.MarkDeveloperMode();
  EXPECT_FALSE(base::PathExists(fake_stateful_.Append(".developer_mode")));

  ASSERT_TRUE(cros_system_->VbSetSystemPropertyString(
      crossystem::Crossystem::kMainFirmwareActive, "recovery"));
  clobber_.MarkDeveloperMode();
  EXPECT_FALSE(base::PathExists(fake_stateful_.Append(".developer_mode")));

  ASSERT_TRUE(cros_system_->VbSetSystemPropertyInt(
      crossystem::Crossystem::kDevSwitchBoot, 1));
  clobber_.MarkDeveloperMode();
  EXPECT_FALSE(base::PathExists(fake_stateful_.Append(".developer_mode")));

  cros_system_->UnsetSystemPropertyValue(
      crossystem::Crossystem::kMainFirmwareActive);
  clobber_.MarkDeveloperMode();
  EXPECT_FALSE(base::PathExists(fake_stateful_.Append(".developer_mode")));
}

TEST_F(MarkDeveloperModeTest, IsDeveloper) {
  ASSERT_TRUE(cros_system_->VbSetSystemPropertyInt(
      crossystem::Crossystem::kDevSwitchBoot, 1));
  ASSERT_TRUE(cros_system_->VbSetSystemPropertyString(
      crossystem::Crossystem::kMainFirmwareActive, "not_recovery"));
  clobber_.MarkDeveloperMode();
  EXPECT_TRUE(base::PathExists(fake_stateful_.Append(".developer_mode")));
}

class GetPreservedFilesListTest : public ::testing::Test {
 protected:
  GetPreservedFilesListTest()
      : crossystem_fake_(std::make_unique<crossystem::fake::CrossystemFake>()),
        cros_system_(crossystem_fake_.get()),
        clobber_ui_null_(std::make_unique<ClobberUi>(DevNull())),
        clobber_wipe_mock_(
            std::make_unique<ClobberWipeMock>(clobber_ui_null_.get())),
        clobber_(ClobberState::Arguments(),
                 std::make_unique<crossystem::Crossystem>(
                     std::move(crossystem_fake_)),
                 std::move(clobber_ui_null_),
                 std::move(clobber_wipe_mock_),
                 std::unique_ptr<ClobberLvm>()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_stateful_ = temp_dir_.GetPath();
    clobber_.SetStatefulForTest(fake_stateful_);

    base::FilePath extensions =
        fake_stateful_.Append("unencrypted/import_extensions/extensions");
    ASSERT_TRUE(base::CreateDirectory(extensions));
    ASSERT_TRUE(
        CreateDirectoryAndWriteFile(extensions.Append("fileA.crx"), ""));
    ASSERT_TRUE(
        CreateDirectoryAndWriteFile(extensions.Append("fileB.crx"), ""));
    ASSERT_TRUE(
        CreateDirectoryAndWriteFile(extensions.Append("fileC.tar"), ""));
    ASSERT_TRUE(
        CreateDirectoryAndWriteFile(extensions.Append("fileD.bmp"), ""));

    base::FilePath dlc_factory =
        fake_stateful_.Append("unencrypted/dlc-factory-images");
    ASSERT_TRUE(base::CreateDirectory(dlc_factory));
    ASSERT_TRUE(CreateDirectoryAndWriteFile(
        dlc_factory.Append("test-dlc1/package/dlc.img"), ""));
    ASSERT_TRUE(CreateDirectoryAndWriteFile(
        dlc_factory.Append("test-dlc2/package/dlc.img"), ""));
    ASSERT_TRUE(
        CreateDirectoryAndWriteFile(dlc_factory.Append("test-dlc3"), ""));
  }

  void SetCompare(std::set<std::string> expected,
                  std::set<base::FilePath> actual) {
    for (const std::string& s : expected) {
      EXPECT_TRUE(actual.count(base::FilePath(s)) == 1)
          << "Expected preserved file not found: " << s;
    }
    for (const base::FilePath& fp : actual) {
      EXPECT_TRUE(expected.count(fp.value()) == 1)
          << "Unexpected preserved file found: " << fp.value();
    }
  }

  std::unique_ptr<crossystem::fake::CrossystemFake> crossystem_fake_;
  crossystem::fake::CrossystemFake* cros_system_;
  std::unique_ptr<ClobberUi> clobber_ui_null_;
  std::unique_ptr<ClobberWipeMock> clobber_wipe_mock_;
  ClobberState clobber_;
  base::ScopedTempDir temp_dir_;
  base::FilePath fake_stateful_;
};

TEST_F(GetPreservedFilesListTest, NoOptions) {
  ASSERT_TRUE(cros_system_->VbSetSystemPropertyInt(
      crossystem::Crossystem::kDebugBuild, 0));
  EXPECT_EQ(clobber_.GetPreservedFilesList().size(), 0);

  ASSERT_TRUE(cros_system_->VbSetSystemPropertyInt(
      crossystem::Crossystem::kDebugBuild, 1));
  std::vector<base::FilePath> preserved_files =
      clobber_.GetPreservedFilesList();
  std::set<base::FilePath> preserved_set(preserved_files.begin(),
                                         preserved_files.end());
  std::set<std::string> expected_preserved_set{".labmachine"};
  SetCompare(expected_preserved_set, preserved_set);
}

TEST_F(GetPreservedFilesListTest, SafeWipe) {
  ClobberState::Arguments args;
  args.safe_wipe = true;
  clobber_.SetArgsForTest(args);

  ASSERT_TRUE(cros_system_->VbSetSystemPropertyInt(
      crossystem::Crossystem::kDebugBuild, 0));
  std::vector<base::FilePath> preserved_files =
      clobber_.GetPreservedFilesList();
  std::set<base::FilePath> preserved_set(preserved_files.begin(),
                                         preserved_files.end());
  std::set<std::string> expected_preserved_set{
      "unencrypted/cros-components/offline-demo-mode-resources/image.squash",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.json",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.sig.1",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.sig.2",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "manifest.fingerprint",
      "unencrypted/cros-components/offline-demo-mode-resources/manifest.json",
      "unencrypted/cros-components/offline-demo-mode-resources/table",
      "unencrypted/preserve/flex/flex_id",
      "unencrypted/preserve/gsc_prev_crash_log_id",
      "unencrypted/preserve/last_active_dates",
      "unencrypted/preserve/powerwash_count",
      "unencrypted/preserve/tpm_firmware_update_request",
      "unencrypted/preserve/update_engine/prefs/last-active-ping-day",
      "unencrypted/preserve/update_engine/prefs/last-roll-call-ping-day",
      "unencrypted/preserve/update_engine/prefs/rollback-happened",
      "unencrypted/preserve/update_engine/prefs/rollback-version"};
  SetCompare(expected_preserved_set, preserved_set);
}

TEST_F(GetPreservedFilesListTest, SafeAndRollbackWipe) {
  ClobberState::Arguments args;
  args.safe_wipe = true;
  args.rollback_wipe = true;
  clobber_.SetArgsForTest(args);
  ASSERT_TRUE(cros_system_->VbSetSystemPropertyInt(
      crossystem::Crossystem::kDebugBuild, 0));

  std::vector<base::FilePath> preserved_files =
      clobber_.GetPreservedFilesList();
  std::set<base::FilePath> preserved_set(preserved_files.begin(),
                                         preserved_files.end());
  std::set<std::string> expected_preserved_set{
      "unencrypted/cros-components/offline-demo-mode-resources/image.squash",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.json",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.sig.1",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.sig.2",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "manifest.fingerprint",
      "unencrypted/cros-components/offline-demo-mode-resources/manifest.json",
      "unencrypted/cros-components/offline-demo-mode-resources/table",
      "unencrypted/preserve/enterprise-rollback-metrics-data",
      "unencrypted/preserve/flex/flex_id",
      "unencrypted/preserve/gsc_prev_crash_log_id",
      "unencrypted/preserve/last_active_dates",
      "unencrypted/preserve/powerwash_count",
      "unencrypted/preserve/rollback_data",
      "unencrypted/preserve/rollback_data_tpm",
      "unencrypted/preserve/tpm_firmware_update_request",
      "unencrypted/preserve/update_engine/prefs/last-active-ping-day",
      "unencrypted/preserve/update_engine/prefs/last-roll-call-ping-day",
      "unencrypted/preserve/update_engine/prefs/rollback-happened",
      "unencrypted/preserve/update_engine/prefs/rollback-version"};
  SetCompare(expected_preserved_set, preserved_set);
}

TEST_F(GetPreservedFilesListTest, SafeAndAdMigrationWipe) {
  ClobberState::Arguments args;
  args.safe_wipe = true;
  args.ad_migration_wipe = true;
  clobber_.SetArgsForTest(args);

  ASSERT_TRUE(cros_system_->VbSetSystemPropertyInt(
      crossystem::Crossystem::kDebugBuild, 0));
  std::vector<base::FilePath> preserved_files =
      clobber_.GetPreservedFilesList();
  std::set<base::FilePath> preserved_set(preserved_files.begin(),
                                         preserved_files.end());
  std::set<std::string> expected_preserved_set{
      "unencrypted/cros-components/offline-demo-mode-resources/image.squash",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.json",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.sig.1",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.sig.2",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "manifest.fingerprint",
      "unencrypted/cros-components/offline-demo-mode-resources/manifest.json",
      "unencrypted/cros-components/offline-demo-mode-resources/table",
      "unencrypted/preserve/chromad_migration_skip_oobe",
      "unencrypted/preserve/flex/flex_id",
      "unencrypted/preserve/gsc_prev_crash_log_id",
      "unencrypted/preserve/last_active_dates",
      "unencrypted/preserve/powerwash_count",
      "unencrypted/preserve/tpm_firmware_update_request",
      "unencrypted/preserve/update_engine/prefs/last-active-ping-day",
      "unencrypted/preserve/update_engine/prefs/last-roll-call-ping-day",
      "unencrypted/preserve/update_engine/prefs/rollback-happened",
      "unencrypted/preserve/update_engine/prefs/rollback-version"};
  SetCompare(expected_preserved_set, preserved_set);
}

TEST_F(GetPreservedFilesListTest, FactoryWipe) {
  ClobberState::Arguments args;
  args.factory_wipe = true;
  clobber_.SetArgsForTest(args);

  ASSERT_TRUE(cros_system_->VbSetSystemPropertyInt(
      crossystem::Crossystem::kDebugBuild, 0));
  std::vector<base::FilePath> preserved_files =
      clobber_.GetPreservedFilesList();
  std::set<base::FilePath> preserved_set(preserved_files.begin(),
                                         preserved_files.end());
  std::set<std::string> expected_preserved_set{
      "unencrypted/dlc-factory-images/test-dlc1/package/dlc.img",
      "unencrypted/dlc-factory-images/test-dlc2/package/dlc.img",
      "unencrypted/import_extensions/extensions/fileA.crx",
      "unencrypted/import_extensions/extensions/fileB.crx"};
  SetCompare(expected_preserved_set, preserved_set);
}

TEST_F(GetPreservedFilesListTest, SafeRollbackFactoryWipe) {
  ClobberState::Arguments args;
  args.safe_wipe = true;
  args.rollback_wipe = true;
  args.factory_wipe = true;
  clobber_.SetArgsForTest(args);

  ASSERT_TRUE(cros_system_->VbSetSystemPropertyInt(
      crossystem::Crossystem::kDebugBuild, 0));
  std::vector<base::FilePath> preserved_files =
      clobber_.GetPreservedFilesList();
  std::set<base::FilePath> preserved_set(preserved_files.begin(),
                                         preserved_files.end());
  std::set<std::string> expected_preserved_set{
      "unencrypted/cros-components/offline-demo-mode-resources/image.squash",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.json",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.sig.1",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.sig.2",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "manifest.fingerprint",
      "unencrypted/cros-components/offline-demo-mode-resources/manifest.json",
      "unencrypted/cros-components/offline-demo-mode-resources/table",
      "unencrypted/dlc-factory-images/test-dlc1/package/dlc.img",
      "unencrypted/dlc-factory-images/test-dlc2/package/dlc.img",
      "unencrypted/import_extensions/extensions/fileA.crx",
      "unencrypted/import_extensions/extensions/fileB.crx",
      "unencrypted/preserve/enterprise-rollback-metrics-data",
      "unencrypted/preserve/flex/flex_id",
      "unencrypted/preserve/gsc_prev_crash_log_id",
      "unencrypted/preserve/last_active_dates",
      "unencrypted/preserve/powerwash_count",
      "unencrypted/preserve/rollback_data",
      "unencrypted/preserve/rollback_data_tpm",
      "unencrypted/preserve/tpm_firmware_update_request",
      "unencrypted/preserve/update_engine/prefs/last-active-ping-day",
      "unencrypted/preserve/update_engine/prefs/last-roll-call-ping-day",
      "unencrypted/preserve/update_engine/prefs/rollback-happened",
      "unencrypted/preserve/update_engine/prefs/rollback-version"};
  SetCompare(expected_preserved_set, preserved_set);
}

class AttemptSwitchToFastWipeTest : public ::testing::Test {
 protected:
  AttemptSwitchToFastWipeTest()
      : clobber_ui_null_(std::make_unique<ClobberUi>(DevNull())),
        clobber_wipe_mock_(
            std::make_unique<ClobberWipeMock>(clobber_ui_null_.get())),
        clobber_wipe_(clobber_wipe_mock_.get()),
        clobber_(ClobberState::Arguments(),
                 std::make_unique<crossystem::Crossystem>(
                     std::make_unique<crossystem::fake::CrossystemFake>()),
                 std::move(clobber_ui_null_),
                 std::move(clobber_wipe_mock_),
                 std::unique_ptr<ClobberLvm>()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    temp_path_ = temp_dir_.GetPath();
    fake_stateful_ = temp_path_.Append("stateful");
    clobber_.SetStatefulForTest(fake_stateful_);

    base::FilePath shadow = fake_stateful_.Append("home/.shadow");

    encrypted_stateful_paths_ = std::vector<base::FilePath>({
        fake_stateful_.Append("encrypted.block"),
        fake_stateful_.Append("var_overlay/fileA"),
        fake_stateful_.Append("var_overlay/fileB"),
        fake_stateful_.Append("dev_image/fileA"),
        fake_stateful_.Append("dev_image/fileB"),
        shadow.Append("uninteresting/vault/fileA"),
        shadow.Append("uninteresting/vault/fileB"),
        shadow.Append("uninteresting/vault/fileC"),
        shadow.Append("other/vault/fileA"),
        shadow.Append("vault/fileA"),
        shadow.Append("vault/fileB"),
    });

    key_material_paths_ = std::vector<base::FilePath>({
        fake_stateful_.Append("encrypted.key"),
        fake_stateful_.Append("encrypted.needs-finalization"),
        fake_stateful_.Append("home/.shadow/cryptohome.key"),
        fake_stateful_.Append("home/.shadow/extra_dir/master"),
        fake_stateful_.Append("home/.shadow/other_dir/master"),
        fake_stateful_.Append("home/.shadow/random_dir/master.0"),
        fake_stateful_.Append("home/.shadow/random_dir/master.1"),
        fake_stateful_.Append(
            "home/.shadow/new_dir/auth_factors/password.first"),
        fake_stateful_.Append(
            "home/.shadow/new_dir/auth_factors/password.second"),
        fake_stateful_.Append("home/.shadow/new_dir/auth_factors/pin.other"),
        fake_stateful_.Append("home/.shadow/new_dir/user_secret_stash/uss.0"),
        fake_stateful_.Append("home/.shadow/salt"),
        fake_stateful_.Append("home/.shadow/salt.sum"),
    });

    shredded_paths_ = std::vector<base::FilePath>(
        {fake_stateful_.Append("really/deeply/buried/random/file/to/delete"),
         fake_stateful_.Append("other/file/to/delete")});

    for (const base::FilePath& path : encrypted_stateful_paths_) {
      ASSERT_TRUE(CreateDirectoryAndWriteFile(path, kContents));
    }

    for (const base::FilePath& path : key_material_paths_) {
      ASSERT_TRUE(CreateDirectoryAndWriteFile(path, kContents));
    }

    for (const base::FilePath& path : shredded_paths_) {
      ASSERT_TRUE(CreateDirectoryAndWriteFile(path, kContents));
    }
  }

  void CheckPathsUntouched(const std::vector<base::FilePath>& paths) {
    for (const base::FilePath& path : paths) {
      std::string contents;
      EXPECT_TRUE(base::ReadFileToString(path, &contents))
          << "Couldn't read " << path.value();
      EXPECT_EQ(contents, kContents);
    }
  }

  void CheckPathsShredded(const std::vector<base::FilePath>& paths) {
    for (const base::FilePath& path : paths) {
      std::string contents;
      EXPECT_TRUE(base::ReadFileToString(path, &contents))
          << "Couldn't read " << path.value();
      EXPECT_NE(contents, kContents);
    }
  }

  void CheckPathsDeleted(const std::vector<base::FilePath>& paths) {
    for (const base::FilePath& path : paths) {
      EXPECT_FALSE(base::PathExists(path))
          << path.value() << " should not exist";
    }
  }

  const std::string kContents = "TOP_SECRET_DATA";

  std::unique_ptr<ClobberUi> clobber_ui_null_;
  std::unique_ptr<ClobberWipeMock> clobber_wipe_mock_;
  ClobberWipeMock* clobber_wipe_;
  ClobberState clobber_;
  base::ScopedTempDir temp_dir_;
  base::FilePath temp_path_;
  base::FilePath fake_stateful_;
  // Files which are deleted by ShredRotationalStatefulPaths.
  std::vector<base::FilePath> encrypted_stateful_paths_;
  // Files which are deleted by WipeKeyMaterial.
  std::vector<base::FilePath> key_material_paths_;
  // Files which will be shredded (overwritten) but not deleted by
  // ShredRotationalStatefulPaths.
  std::vector<base::FilePath> shredded_paths_;
};

TEST_F(AttemptSwitchToFastWipeTest, NotRotationalNoSecureErase) {
  ClobberState::Arguments args;
  args.fast_wipe = false;
  clobber_.SetArgsForTest(args);

  clobber_wipe_->SetSecureEraseSupported(false);
  clobber_.AttemptSwitchToFastWipe(false);
  EXPECT_FALSE(clobber_.GetArgsForTest().fast_wipe);
  CheckPathsUntouched(encrypted_stateful_paths_);
  CheckPathsUntouched(key_material_paths_);
  CheckPathsUntouched(shredded_paths_);
}

TEST_F(AttemptSwitchToFastWipeTest, AlreadyFast) {
  ClobberState::Arguments args;
  args.fast_wipe = true;
  clobber_.SetArgsForTest(args);

  clobber_wipe_->SetSecureEraseSupported(true);
  clobber_.AttemptSwitchToFastWipe(true);
  EXPECT_TRUE(clobber_.GetArgsForTest().fast_wipe);
  CheckPathsUntouched(encrypted_stateful_paths_);
  CheckPathsUntouched(key_material_paths_);
  CheckPathsUntouched(shredded_paths_);
}

TEST_F(AttemptSwitchToFastWipeTest, RotationalNoSecureErase) {
  ClobberState::Arguments args;
  args.fast_wipe = false;
  clobber_.SetArgsForTest(args);

  clobber_wipe_->SetSecureEraseSupported(false);
  clobber_.AttemptSwitchToFastWipe(true);
  EXPECT_TRUE(clobber_.GetArgsForTest().fast_wipe);
  CheckPathsDeleted(encrypted_stateful_paths_);
  CheckPathsShredded(key_material_paths_);
  CheckPathsShredded(shredded_paths_);
}

TEST_F(AttemptSwitchToFastWipeTest, SecureEraseNotRotational) {
  ClobberState::Arguments args;
  args.fast_wipe = false;
  clobber_.SetArgsForTest(args);

  clobber_wipe_->SetSecureEraseSupported(true);
  clobber_.AttemptSwitchToFastWipe(false);
  EXPECT_TRUE(clobber_.GetArgsForTest().fast_wipe);
  CheckPathsUntouched(encrypted_stateful_paths_);
  CheckPathsDeleted(key_material_paths_);
  CheckPathsUntouched(shredded_paths_);
}

TEST_F(AttemptSwitchToFastWipeTest, SecureEraseNotRotationalFactoryWipe) {
  ClobberState::Arguments args;
  args.fast_wipe = false;
  args.factory_wipe = true;
  clobber_.SetArgsForTest(args);

  clobber_wipe_->SetSecureEraseSupported(true);
  clobber_.AttemptSwitchToFastWipe(false);
  EXPECT_TRUE(clobber_.GetArgsForTest().fast_wipe);
  CheckPathsUntouched(encrypted_stateful_paths_);
  CheckPathsDeleted(key_material_paths_);
  CheckPathsUntouched(shredded_paths_);
}

TEST_F(AttemptSwitchToFastWipeTest, RotationalSecureErase) {
  ClobberState::Arguments args;
  args.fast_wipe = false;
  clobber_.SetArgsForTest(args);

  clobber_wipe_->SetSecureEraseSupported(true);
  clobber_.AttemptSwitchToFastWipe(true);
  EXPECT_TRUE(clobber_.GetArgsForTest().fast_wipe);
  CheckPathsDeleted(encrypted_stateful_paths_);
  CheckPathsShredded(key_material_paths_);
  CheckPathsShredded(shredded_paths_);
}

TEST_F(AttemptSwitchToFastWipeTest, RotationalSecureEraseFactoryWipe) {
  ClobberState::Arguments args;
  args.fast_wipe = false;
  args.factory_wipe = true;
  clobber_.SetArgsForTest(args);

  clobber_wipe_->SetSecureEraseSupported(true);
  clobber_.AttemptSwitchToFastWipe(true);
  EXPECT_TRUE(clobber_.GetArgsForTest().fast_wipe);
  CheckPathsDeleted(encrypted_stateful_paths_);
  CheckPathsShredded(key_material_paths_);
  CheckPathsShredded(shredded_paths_);
}

class ShredRotationalStatefulFilesTest : public ::testing::Test {
 protected:
  ShredRotationalStatefulFilesTest()
      : clobber_ui_null_(std::make_unique<ClobberUi>(DevNull())),
        clobber_wipe_mock_(
            std::make_unique<ClobberWipeMock>(clobber_ui_null_.get())),
        clobber_(ClobberState::Arguments(),
                 std::make_unique<crossystem::Crossystem>(
                     std::make_unique<crossystem::fake::CrossystemFake>()),
                 std::move(clobber_ui_null_),
                 std::move(clobber_wipe_mock_),
                 std::unique_ptr<ClobberLvm>()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    temp_path_ = temp_dir_.GetPath();
    fake_stateful_ = temp_path_.Append("stateful");
    clobber_.SetStatefulForTest(fake_stateful_);

    base::FilePath shadow = fake_stateful_.Append("home/.shadow");

    deleted_paths_ = std::vector<base::FilePath>({
        fake_stateful_.Append("dev_image/fileA"),
        fake_stateful_.Append("dev_image/fileB"),
        fake_stateful_.Append("encrypted.block"),
        fake_stateful_.Append("var_overlay/fileA"),
        fake_stateful_.Append("var_overlay/fileB"),
        shadow.Append("other/vault/fileA"),
        shadow.Append("uninteresting/vault/fileA"),
        shadow.Append("uninteresting/vault/fileB"),
        shadow.Append("uninteresting/vault/fileC"),
        shadow.Append("vault/fileA"),
        shadow.Append("vault/fileB"),
    });

    shredded_paths_ = std::vector<base::FilePath>(
        {fake_stateful_.Append("really/deeply/buried/random/file/to/delete"),
         fake_stateful_.Append("other/file/to/delete")});

    for (const base::FilePath& path : deleted_paths_) {
      ASSERT_TRUE(CreateDirectoryAndWriteFile(path, kContents));
    }

    for (const base::FilePath& path : shredded_paths_) {
      ASSERT_TRUE(CreateDirectoryAndWriteFile(path, kContents));
    }
  }

  void CheckPathsUntouched(const std::vector<base::FilePath>& paths) {
    for (const base::FilePath& path : paths) {
      std::string contents;
      EXPECT_TRUE(base::ReadFileToString(path, &contents))
          << "Couldn't read " << path.value();
      EXPECT_EQ(contents, kContents);
    }
  }

  void CheckPathsShredded(const std::vector<base::FilePath>& paths) {
    for (const base::FilePath& path : paths) {
      std::string contents;
      EXPECT_TRUE(base::ReadFileToString(path, &contents))
          << "Couldn't read " << path.value();
      EXPECT_NE(contents, kContents);
    }
  }

  void CheckPathsDeleted(const std::vector<base::FilePath>& paths) {
    for (const base::FilePath& path : paths) {
      EXPECT_FALSE(base::PathExists(path))
          << path.value() << " should not exist";
    }
  }

  const std::string kContents = "TOP_SECRET_DATA";

  std::unique_ptr<ClobberUi> clobber_ui_null_;
  std::unique_ptr<ClobberWipeMock> clobber_wipe_mock_;
  ClobberState clobber_;
  base::ScopedTempDir temp_dir_;
  base::FilePath temp_path_;
  base::FilePath fake_stateful_;
  // Files which are deleted by ShredRotationalStatefulPaths.
  std::vector<base::FilePath> deleted_paths_;
  // Files which will be shredded (overwritten) but not deleted by
  // ShredRotationalStatefulPaths.
  std::vector<base::FilePath> shredded_paths_;
};

TEST_F(ShredRotationalStatefulFilesTest, Mounted) {
  clobber_.ShredRotationalStatefulFiles();
  CheckPathsDeleted(deleted_paths_);
  CheckPathsShredded(shredded_paths_);
}

class WipeCryptohomeTest : public ::testing::Test {
 protected:
  WipeCryptohomeTest()
      : clobber_ui_null_(std::make_unique<ClobberUi>(DevNull())),
        clobber_wipe_mock_(
            std::make_unique<ClobberWipeMock>(clobber_ui_null_.get())),
        clobber_wipe_(clobber_wipe_mock_.get()),
        clobber_(ClobberState::Arguments(),
                 std::make_unique<crossystem::Crossystem>(
                     std::make_unique<crossystem::fake::CrossystemFake>()),
                 std::move(clobber_ui_null_),
                 std::move(clobber_wipe_mock_),
                 std::unique_ptr<ClobberLvm>()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_stateful_ = temp_dir_.GetPath();
    clobber_.SetStatefulForTest(fake_stateful_);

    deleted_paths_ = std::vector<base::FilePath>({
        fake_stateful_.Append("encrypted.key"),
        fake_stateful_.Append("encrypted.needs-finalization"),
        fake_stateful_.Append("home/.shadow/cryptohome.key"),
        fake_stateful_.Append("home/.shadow/extra_dir/master"),
        fake_stateful_.Append("home/.shadow/other_dir/master"),
        fake_stateful_.Append("home/.shadow/random_dir/master.0"),
        fake_stateful_.Append("home/.shadow/random_dir/master.1"),
        fake_stateful_.Append(
            "home/.shadow/new_dir/auth_factors/password.first"),
        fake_stateful_.Append(
            "home/.shadow/new_dir/auth_factors/password.second"),
        fake_stateful_.Append("home/.shadow/new_dir/auth_factors/pin.other"),
        fake_stateful_.Append("home/.shadow/new_dir/user_secret_stash/uss.0"),
        fake_stateful_.Append("home/.shadow/salt"),
        fake_stateful_.Append("home/.shadow/salt.sum"),
    });

    ignored_paths_ = std::vector<base::FilePath>({
        fake_stateful_.Append("home/.shadow/extra_dir/unimportant"),
        fake_stateful_.Append("home/.shadow/other_dir/unimportant"),
        fake_stateful_.Append("hopefully/not/a/copy/of/etc/passwd"),
        fake_stateful_.Append("uninteresting/file/definitely/not/an/rsa/key"),
    });

    for (const base::FilePath& path : deleted_paths_) {
      ASSERT_TRUE(CreateDirectoryAndWriteFile(path, kContents));
    }

    for (const base::FilePath& path : ignored_paths_) {
      ASSERT_TRUE(CreateDirectoryAndWriteFile(path, kContents));
    }
  }

  void CheckPathsUntouched(const std::vector<base::FilePath>& paths) {
    for (const base::FilePath& path : paths) {
      std::string contents;
      EXPECT_TRUE(base::ReadFileToString(path, &contents))
          << "Couldn't read " << path.value();
      EXPECT_EQ(contents, kContents);
    }
  }

  void CheckPathsDeleted(const std::vector<base::FilePath>& paths) {
    for (const base::FilePath& path : paths) {
      EXPECT_FALSE(base::PathExists(path))
          << path.value() << " should not exist";
    }
  }

  const std::string kContents = "feebdabdeefedaceddad";

  std::unique_ptr<ClobberUi> clobber_ui_null_;
  std::unique_ptr<ClobberWipeMock> clobber_wipe_mock_;
  ClobberWipeMock* clobber_wipe_;
  ClobberState clobber_;
  base::ScopedTempDir temp_dir_;
  base::FilePath fake_stateful_;
  std::vector<base::FilePath> deleted_paths_;
  std::vector<base::FilePath> ignored_paths_;
};

TEST_F(WipeCryptohomeTest, NotSupported) {
  clobber_wipe_->SetSecureEraseSupported(false);
  CheckPathsUntouched(deleted_paths_);
  CheckPathsUntouched(ignored_paths_);

  EXPECT_FALSE(clobber_.WipeKeyMaterial());

  CheckPathsUntouched(ignored_paths_);
}

TEST_F(WipeCryptohomeTest, Supported) {
  clobber_wipe_->SetSecureEraseSupported(true);
  CheckPathsUntouched(deleted_paths_);
  CheckPathsUntouched(ignored_paths_);

  EXPECT_TRUE(clobber_.WipeKeyMaterial());

  CheckPathsDeleted(deleted_paths_);
  CheckPathsUntouched(ignored_paths_);
}

class GetDevicesToWipeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    partitions_.stateful = 1;
    partitions_.kernel_a = 2;
    partitions_.root_a = 3;
    partitions_.kernel_b = 4;
    partitions_.root_b = 5;
  }

  ClobberWipe::PartitionNumbers partitions_;
};

TEST_F(GetDevicesToWipeTest, Error) {
  base::FilePath root_disk("/dev/sda");
  base::FilePath root_device("/dev/sda4");

  ClobberState::DeviceWipeInfo wipe_info;
  // Partition number for root_device does not match root_a or root_b in
  // partitions_ struct.
  EXPECT_FALSE(ClobberState::GetDevicesToWipe(root_disk, root_device,
                                              partitions_, &wipe_info));
}

TEST_F(GetDevicesToWipeTest, MMC) {
  base::FilePath root_disk("/dev/mmcblk0");
  base::FilePath root_device("/dev/mmcblk0p3");

  ClobberState::DeviceWipeInfo wipe_info;
  EXPECT_TRUE(ClobberState::GetDevicesToWipe(root_disk, root_device,
                                             partitions_, &wipe_info));
  EXPECT_EQ(wipe_info.stateful_partition_device.value(), "/dev/mmcblk0p1");
  EXPECT_EQ(wipe_info.inactive_root_device.value(), "/dev/mmcblk0p5");
  EXPECT_EQ(wipe_info.inactive_kernel_device.value(), "/dev/mmcblk0p4");
  EXPECT_FALSE(wipe_info.is_mtd_flash);
  EXPECT_EQ(wipe_info.active_kernel_partition, partitions_.kernel_a);
}

TEST_F(GetDevicesToWipeTest, NVME_a_active) {
  base::FilePath root_disk("/dev/nvme0n1");
  base::FilePath root_device("/dev/nvme0n1p3");

  ClobberState::DeviceWipeInfo wipe_info;
  EXPECT_TRUE(ClobberState::GetDevicesToWipe(root_disk, root_device,
                                             partitions_, &wipe_info));
  EXPECT_EQ(wipe_info.stateful_partition_device.value(), "/dev/nvme0n1p1");
  EXPECT_EQ(wipe_info.inactive_root_device.value(), "/dev/nvme0n1p5");
  EXPECT_EQ(wipe_info.inactive_kernel_device.value(), "/dev/nvme0n1p4");
  EXPECT_FALSE(wipe_info.is_mtd_flash);
  EXPECT_EQ(wipe_info.active_kernel_partition, partitions_.kernel_a);
}

TEST_F(GetDevicesToWipeTest, NVME_b_active) {
  base::FilePath root_disk("/dev/nvme0n1");
  base::FilePath root_device("/dev/nvme0n1p5");

  ClobberState::DeviceWipeInfo wipe_info;
  EXPECT_TRUE(ClobberState::GetDevicesToWipe(root_disk, root_device,
                                             partitions_, &wipe_info));
  EXPECT_EQ(wipe_info.stateful_partition_device.value(), "/dev/nvme0n1p1");
  EXPECT_EQ(wipe_info.inactive_root_device.value(), "/dev/nvme0n1p3");
  EXPECT_EQ(wipe_info.inactive_kernel_device.value(), "/dev/nvme0n1p2");
  EXPECT_FALSE(wipe_info.is_mtd_flash);
  EXPECT_EQ(wipe_info.active_kernel_partition, partitions_.kernel_b);
}

TEST_F(GetDevicesToWipeTest, UFS) {
  base::FilePath root_disk("/dev/sda1");
  base::FilePath root_device("/dev/sda5");

  ClobberState::DeviceWipeInfo wipe_info;
  EXPECT_TRUE(ClobberState::GetDevicesToWipe(root_disk, root_device,
                                             partitions_, &wipe_info));
  EXPECT_EQ(wipe_info.stateful_partition_device.value(), "/dev/sda1");
  EXPECT_EQ(wipe_info.inactive_root_device.value(), "/dev/sda3");
  EXPECT_EQ(wipe_info.inactive_kernel_device.value(), "/dev/sda2");
  EXPECT_FALSE(wipe_info.is_mtd_flash);
  EXPECT_EQ(wipe_info.active_kernel_partition, partitions_.kernel_b);
}

TEST_F(GetDevicesToWipeTest, SDA) {
  partitions_.stateful = 7;
  partitions_.kernel_a = 1;
  partitions_.root_a = 9;
  partitions_.kernel_b = 2;
  partitions_.root_b = 4;

  base::FilePath root_disk("/dev/sda");
  base::FilePath root_device("/dev/sda9");

  ClobberState::DeviceWipeInfo wipe_info;
  EXPECT_TRUE(ClobberState::GetDevicesToWipe(root_disk, root_device,
                                             partitions_, &wipe_info));
  EXPECT_EQ(wipe_info.stateful_partition_device.value(), "/dev/sda7");
  EXPECT_EQ(wipe_info.inactive_root_device.value(), "/dev/sda4");
  EXPECT_EQ(wipe_info.inactive_kernel_device.value(), "/dev/sda2");
  EXPECT_FALSE(wipe_info.is_mtd_flash);
  EXPECT_EQ(wipe_info.active_kernel_partition, partitions_.kernel_a);
}

class PreserveEncryptedFilesTest : public ::testing::Test {
 protected:
  PreserveEncryptedFilesTest()
      : crossystem_fake_(std::make_unique<crossystem::fake::CrossystemFake>()),
        cros_system_(crossystem_fake_.get()),
        clobber_ui_null_(std::make_unique<ClobberUi>(DevNull())),
        clobber_wipe_mock_(
            std::make_unique<ClobberWipeMock>(clobber_ui_null_.get())),
        clobber_(ClobberState::Arguments(),
                 std::make_unique<crossystem::Crossystem>(
                     std::move(crossystem_fake_)),
                 std::move(clobber_ui_null_),
                 std::move(clobber_wipe_mock_),
                 std::unique_ptr<ClobberLvm>()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_stateful_.CreateUniqueTempDir());
    fake_stateful_ = temp_stateful_.GetPath();
    clobber_.SetStatefulForTest(fake_stateful_);

    ASSERT_TRUE(temp_root_.CreateUniqueTempDir());
    fake_root_ = temp_root_.GetPath();
    clobber_.SetRootPathForTest(fake_root_);
  }

  std::unique_ptr<crossystem::fake::CrossystemFake> crossystem_fake_;
  crossystem::fake::CrossystemFake* cros_system_;
  std::unique_ptr<ClobberUi> clobber_ui_null_;
  std::unique_ptr<ClobberWipeMock> clobber_wipe_mock_;
  ClobberState clobber_;
  base::ScopedTempDir temp_root_;
  base::ScopedTempDir temp_stateful_;
  base::FilePath fake_root_;
  base::FilePath fake_stateful_;
};

TEST_F(PreserveEncryptedFilesTest, UpdateEnginePrefsArePreserved) {
  ASSERT_TRUE(CreateDirectoryAndWriteFile(
      fake_root_.Append("var/lib/update_engine/prefs/last-active-ping-day"),
      "1234"));
  ASSERT_TRUE(CreateDirectoryAndWriteFile(
      fake_root_.Append("var/lib/update_engine/prefs/last-roll-call-ping-day"),
      "5678"));
  clobber_.PreserveEncryptedFiles();
  ASSERT_TRUE(base::PathExists(
      fake_stateful_.Append("unencrypted/preserve/update_engine/prefs/")));
  ASSERT_TRUE(base::PathExists(fake_stateful_.Append(
      "unencrypted/preserve/update_engine/prefs/last-active-ping-day")));
  ASSERT_TRUE(base::PathExists(fake_stateful_.Append(
      "unencrypted/preserve/update_engine/prefs/last-roll-call-ping-day")));
}

TEST_F(PreserveEncryptedFilesTest, PsmPrefsArePreserved) {
  ASSERT_TRUE(CreateDirectoryAndWriteFile(
      fake_root_.Append("var/lib/private_computing/last_active_dates"),
      "1234"));
  clobber_.PreserveEncryptedFiles();
  ASSERT_TRUE(base::PathExists(
      fake_stateful_.Append("unencrypted/preserve/last_active_dates")));
}

TEST_F(PreserveEncryptedFilesTest, FlexFilesArePreserved) {
  ASSERT_TRUE(CreateDirectoryAndWriteFile(
      fake_root_.Append("var/lib/flex_id/flex_id"), "1234"));
  clobber_.PreserveEncryptedFiles();
  ASSERT_TRUE(base::PathExists(
      fake_stateful_.Append("unencrypted/preserve/flex/flex_id")));
}
