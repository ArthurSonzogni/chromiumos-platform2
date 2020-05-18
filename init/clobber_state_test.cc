// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/clobber_state.h"

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
#include <brillo/process.h>
#include <gtest/gtest.h>

#include "init/crossystem.h"
#include "init/crossystem_fake.h"

namespace {
// Commands for disk formatting utility sfdisk.
// Specify that partition table should use gpt format.
constexpr char kSfdiskPartitionTableTypeCommand[] = "label: gpt\n";
// Templates for partition command (size specified in number of sectors).
constexpr char kSfdiskCommandFormat[] = "size=1, type=%s, name=\"%s\"\n";
constexpr char kSfdiskCommandWithAttrsFormat[] =
    "size=1, type=%s, name=\"%s\", attrs=\"%s\"\n";

// UUIDs for various partition types in gpt partition tables.
constexpr char kKernelPartition[] = "FE3A2A5D-4F32-41A7-B725-ACCC3285A309";
constexpr char kRootPartition[] = "3CB8E202-3B7E-47DD-8A3C-7FF2A13CFCEC";
constexpr char kDataPartition[] = "0FC63DAF-8483-4772-8E79-3D69D8477DE4";
constexpr char kReservedPartition[] = "2E0A753D-9E48-43B0-8337-B15192CB1B5E";
constexpr char kRWFWPartition[] = "CAB6E88E-ABF3-4102-A07A-D4BB9BE3C1D3";
constexpr char kEFIPartition[] = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B";

bool WriteFile(const base::FilePath& path, const std::string& contents) {
  return base::CreateDirectory(path.DirName()) &&
         base::WriteFile(path, contents.c_str(), contents.length()) ==
             contents.length();
}

base::File DevNull() {
  return base::File(base::FilePath("/dev/null"),
                    base::File::FLAG_OPEN | base::File::FLAG_WRITE);
}
}  // namespace

TEST(ParseArgv, EmptyArgs) {
  std::vector<const char*> argv{"clobber-state"};
  ClobberState::Arguments args = ClobberState::ParseArgv(argv.size(), &argv[0]);
  EXPECT_FALSE(args.factory_wipe);
  EXPECT_FALSE(args.fast_wipe);
  EXPECT_FALSE(args.keepimg);
  EXPECT_FALSE(args.safe_wipe);
  EXPECT_FALSE(args.rollback_wipe);
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
}

TEST(ParseArgv, SomeArgsIndividual) {
  std::vector<const char*> argv{"clobber-state", "rollback", "fast", "keepimg"};
  ClobberState::Arguments args = ClobberState::ParseArgv(argv.size(), &argv[0]);
  EXPECT_FALSE(args.factory_wipe);
  EXPECT_TRUE(args.fast_wipe);
  EXPECT_TRUE(args.keepimg);
  EXPECT_FALSE(args.safe_wipe);
  EXPECT_TRUE(args.rollback_wipe);
}

TEST(ParseArgv, SomeArgsSquished) {
  std::vector<const char*> argv{"clobber-state", "rollback safe fast"};
  ClobberState::Arguments args = ClobberState::ParseArgv(argv.size(), &argv[0]);
  EXPECT_FALSE(args.factory_wipe);
  EXPECT_TRUE(args.fast_wipe);
  EXPECT_FALSE(args.keepimg);
  EXPECT_TRUE(args.safe_wipe);
  EXPECT_TRUE(args.rollback_wipe);
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
  ASSERT_TRUE(WriteFile(counter, "-3\n"));
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "1\n");
}

TEST(IncrementFileCounter, SmallNumber) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath counter = temp_dir.GetPath().Append("counter");
  ASSERT_TRUE(WriteFile(counter, "42\n"));
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "43\n");
}

TEST(IncrementFileCounter, LargeNumber) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath counter = temp_dir.GetPath().Append("counter");
  ASSERT_TRUE(WriteFile(counter, "1238761\n"));
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "1238762\n");
}

TEST(IncrementFileCounter, NonNumber) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath counter = temp_dir.GetPath().Append("counter");
  ASSERT_TRUE(WriteFile(counter, "cruciverbalist"));
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "1\n");
}

TEST(IncrementFileCounter, IntMax) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath counter = temp_dir.GetPath().Append("counter");
  ASSERT_TRUE(WriteFile(counter, std::to_string(INT_MAX)));
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "1\n");
}

TEST(IncrementFileCounter, LongMax) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath counter = temp_dir.GetPath().Append("counter");
  ASSERT_TRUE(WriteFile(counter, std::to_string(LONG_MAX)));
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "1\n");
}

TEST(IncrementFileCounter, InputNoNewline) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath counter = temp_dir.GetPath().Append("counter");
  ASSERT_TRUE(WriteFile(counter, std::to_string(7)));
  EXPECT_TRUE(ClobberState::IncrementFileCounter(counter));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(counter, &contents));
  EXPECT_EQ(contents, "8\n");
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

  ASSERT_TRUE(WriteFile(tar_file, ""));
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

  ASSERT_TRUE(WriteFile(tar_file, ""));
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

  ASSERT_TRUE(WriteFile(stateful_not_preserved, "unneeded"));
  ASSERT_TRUE(WriteFile(stateful_preserved, "test_contents"));

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

  ASSERT_TRUE(WriteFile(stateful_not_preserved, "unneeded"));
  ASSERT_TRUE(WriteFile(stateful_preserved_a, "test_contents"));
  ASSERT_TRUE(WriteFile(stateful_preserved_b, "data"));

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

TEST(GetDevicePathComponents, ErrorCases) {
  std::string base_device;
  int partition_number;
  EXPECT_FALSE(ClobberState::GetDevicePathComponents(base::FilePath(""),
                                                     nullptr, nullptr));
  EXPECT_FALSE(ClobberState::GetDevicePathComponents(
      base::FilePath(""), nullptr, &partition_number));
  EXPECT_FALSE(ClobberState::GetDevicePathComponents(base::FilePath(""),
                                                     &base_device, nullptr));
  EXPECT_FALSE(ClobberState::GetDevicePathComponents(
      base::FilePath(""), &base_device, &partition_number));
  EXPECT_FALSE(ClobberState::GetDevicePathComponents(
      base::FilePath("24728"), &base_device, &partition_number));
  EXPECT_FALSE(ClobberState::GetDevicePathComponents(
      base::FilePath("bad_dev"), &base_device, &partition_number));
  EXPECT_FALSE(ClobberState::GetDevicePathComponents(
      base::FilePath("/dev/"), &base_device, &partition_number));
}

TEST(GetDevicePathComponents, ValidCases) {
  std::string base_device;
  int partition_number;
  EXPECT_TRUE(ClobberState::GetDevicePathComponents(
      base::FilePath("/dev/sda273"), &base_device, &partition_number));
  EXPECT_EQ(base_device, "/dev/sda");
  EXPECT_EQ(partition_number, 273);

  EXPECT_TRUE(ClobberState::GetDevicePathComponents(
      base::FilePath("/dev/mmcblk5p193448"), &base_device, &partition_number));
  EXPECT_EQ(base_device, "/dev/mmcblk5p");
  EXPECT_EQ(partition_number, 193448);

  EXPECT_TRUE(ClobberState::GetDevicePathComponents(
      base::FilePath("/dev/nvme7n2p11"), &base_device, &partition_number));
  EXPECT_EQ(base_device, "/dev/nvme7n2p");
  EXPECT_EQ(partition_number, 11);

  EXPECT_TRUE(ClobberState::GetDevicePathComponents(
      base::FilePath("/dev/ubiblock17_0"), &base_device, &partition_number));
  EXPECT_EQ(base_device, "/dev/ubiblock");
  EXPECT_EQ(partition_number, 17);

  EXPECT_TRUE(ClobberState::GetDevicePathComponents(
      base::FilePath("/dev/ubi9_0"), &base_device, &partition_number));
  EXPECT_EQ(base_device, "/dev/ubi");
  EXPECT_EQ(partition_number, 9);

  EXPECT_TRUE(ClobberState::GetDevicePathComponents(
      base::FilePath("/dev/mtd0"), &base_device, &partition_number));
  EXPECT_EQ(base_device, "/dev/mtd");
  EXPECT_EQ(partition_number, 0);
}

class CgptTest : public testing::Test {
 protected:
  void SetUp() override {
    constexpr int kSectorSize = 512;
    constexpr int kSectorCount = 25 * 1024;

    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    test_image_path_ = temp_dir_.GetPath().Append("test.img");
    base::File test_image(test_image_path_,
                          base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(test_image.IsValid());
    ASSERT_GE(test_image.SetLength(kSectorSize * kSectorCount), 0);
    test_image.Close();

    base::FilePath sfdisk_input_path =
        temp_dir_.GetPath().Append("sfdisk_input");
    base::File sfdisk_input(sfdisk_input_path,
                            base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    ASSERT_TRUE(sfdisk_input.IsValid());
    std::vector<std::string> sfdisk_commands{
        kSfdiskPartitionTableTypeCommand,
        base::StringPrintf(kSfdiskCommandFormat, kDataPartition, "STATE"),
        base::StringPrintf(kSfdiskCommandWithAttrsFormat, kKernelPartition,
                           "KERN-A", "GUID:49,56"),
        base::StringPrintf(kSfdiskCommandFormat, kRootPartition, "ROOT-A"),
        base::StringPrintf(kSfdiskCommandWithAttrsFormat, kKernelPartition,
                           "KERN-B", "GUID:48"),
        base::StringPrintf(kSfdiskCommandFormat, kRootPartition, "ROOT-B"),
        base::StringPrintf(kSfdiskCommandWithAttrsFormat, kKernelPartition,
                           "KERN-C", "GUID:52,53,54,55"),
        base::StringPrintf(kSfdiskCommandFormat, kRootPartition, "ROOT-C"),
        base::StringPrintf(kSfdiskCommandFormat, kDataPartition, "OEM"),
        base::StringPrintf(kSfdiskCommandFormat, kReservedPartition,
                           "reserved"),
        base::StringPrintf(kSfdiskCommandFormat, kReservedPartition,
                           "reserved"),
        base::StringPrintf(kSfdiskCommandFormat, kRWFWPartition, "RWFW"),
        base::StringPrintf(kSfdiskCommandFormat, kEFIPartition, "EFI-SYSTEM")};
    for (const std::string& command : sfdisk_commands) {
      EXPECT_EQ(
          sfdisk_input.WriteAtCurrentPos(command.c_str(), command.length()),
          command.length());
    }
    sfdisk_input.Close();

    // Build partition table on backing file.
    brillo::ProcessImpl sfdisk;
    sfdisk.AddArg("/sbin/sfdisk");
    sfdisk.AddArg(test_image_path_.value());
    sfdisk.RedirectInput(sfdisk_input_path.value());
    ASSERT_EQ(sfdisk.Run(), 0);
  }

  base::FilePath test_image_path_;

 private:
  base::ScopedTempDir temp_dir_;
};

TEST_F(CgptTest, FindInvalidPartitions) {
  EXPECT_EQ(ClobberState::GetPartitionNumber(test_image_path_, ""), -1);
  EXPECT_EQ(ClobberState::GetPartitionNumber(test_image_path_, "NONEXISTENT"),
            -1);
  // return -1 here because there are multiple partitions labeled "reserved".
  EXPECT_EQ(ClobberState::GetPartitionNumber(test_image_path_, "reserved"), -1);
}

TEST_F(CgptTest, FindValidPartitions) {
  EXPECT_EQ(ClobberState::GetPartitionNumber(test_image_path_, "STATE"), 1);
  EXPECT_EQ(ClobberState::GetPartitionNumber(test_image_path_, "KERN-A"), 2);
  EXPECT_EQ(ClobberState::GetPartitionNumber(test_image_path_, "ROOT-A"), 3);
  EXPECT_EQ(ClobberState::GetPartitionNumber(test_image_path_, "KERN-B"), 4);
  EXPECT_EQ(ClobberState::GetPartitionNumber(test_image_path_, "ROOT-B"), 5);
  EXPECT_EQ(ClobberState::GetPartitionNumber(test_image_path_, "KERN-C"), 6);
  EXPECT_EQ(ClobberState::GetPartitionNumber(test_image_path_, "ROOT-C"), 7);
  EXPECT_EQ(ClobberState::GetPartitionNumber(test_image_path_, "OEM"), 8);
  EXPECT_EQ(ClobberState::GetPartitionNumber(test_image_path_, "RWFW"), 11);
  EXPECT_EQ(ClobberState::GetPartitionNumber(test_image_path_, "EFI-SYSTEM"),
            12);
}

TEST_F(CgptTest, ReadPartitionMetadata) {
  bool successful;
  int priority;
  EXPECT_TRUE(ClobberState::ReadPartitionMetadata(test_image_path_, 2,
                                                  &successful, &priority));
  EXPECT_TRUE(successful);
  EXPECT_EQ(priority, 2);
  EXPECT_TRUE(ClobberState::ReadPartitionMetadata(test_image_path_, 4,
                                                  &successful, &priority));
  EXPECT_FALSE(successful);
  EXPECT_EQ(priority, 1);
  EXPECT_TRUE(ClobberState::ReadPartitionMetadata(test_image_path_, 6,
                                                  &successful, &priority));
  EXPECT_FALSE(successful);
  EXPECT_EQ(priority, 0);
}

TEST_F(CgptTest, EnsureKernelIsBootable) {
  ClobberState::EnsureKernelIsBootable(test_image_path_, 4);
  bool successful;
  int priority;
  EXPECT_TRUE(ClobberState::ReadPartitionMetadata(test_image_path_, 4,
                                                  &successful, &priority));
  EXPECT_TRUE(successful);
  EXPECT_GT(priority, 0);

  ClobberState::EnsureKernelIsBootable(test_image_path_, 6);
  EXPECT_TRUE(ClobberState::ReadPartitionMetadata(test_image_path_, 6,
                                                  &successful, &priority));
  EXPECT_TRUE(successful);
  EXPECT_GT(priority, 0);
}

class MarkDeveloperModeTest : public ::testing::Test {
 protected:
  MarkDeveloperModeTest()
      : cros_system_(new CrosSystemFake()),
        clobber_(ClobberState::Arguments(),
                 std::unique_ptr<CrosSystem>(cros_system_),
                 std::make_unique<ClobberUi>(DevNull())) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_stateful_ = temp_dir_.GetPath();
    clobber_.SetStatefulForTest(fake_stateful_);
  }

  CrosSystemFake* cros_system_;
  ClobberState clobber_;
  base::ScopedTempDir temp_dir_;
  base::FilePath fake_stateful_;
};

TEST_F(MarkDeveloperModeTest, NotDeveloper) {
  clobber_.MarkDeveloperMode();
  EXPECT_FALSE(base::PathExists(fake_stateful_.Append(".developer_mode")));

  ASSERT_TRUE(cros_system_->SetInt(CrosSystem::kDevSwitchBoot, 0));
  clobber_.MarkDeveloperMode();
  EXPECT_FALSE(base::PathExists(fake_stateful_.Append(".developer_mode")));

  ASSERT_TRUE(
      cros_system_->SetString(CrosSystem::kMainFirmwareActive, "recovery"));
  clobber_.MarkDeveloperMode();
  EXPECT_FALSE(base::PathExists(fake_stateful_.Append(".developer_mode")));

  ASSERT_TRUE(cros_system_->SetInt(CrosSystem::kDevSwitchBoot, 1));
  clobber_.MarkDeveloperMode();
  EXPECT_FALSE(base::PathExists(fake_stateful_.Append(".developer_mode")));
}

TEST_F(MarkDeveloperModeTest, IsDeveloper) {
  ASSERT_TRUE(cros_system_->SetInt(CrosSystem::kDevSwitchBoot, 1));
  ASSERT_TRUE(
      cros_system_->SetString(CrosSystem::kMainFirmwareActive, "not_recovery"));
  clobber_.MarkDeveloperMode();
  EXPECT_TRUE(base::PathExists(fake_stateful_.Append(".developer_mode")));
}

class GetPreservedFilesListTest : public ::testing::Test {
 protected:
  GetPreservedFilesListTest()
      : cros_system_(new CrosSystemFake()),
        clobber_(ClobberState::Arguments(),
                 std::unique_ptr<CrosSystem>(cros_system_),
                 std::make_unique<ClobberUi>(DevNull())) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_stateful_ = temp_dir_.GetPath();
    clobber_.SetStatefulForTest(fake_stateful_);

    base::FilePath extensions =
        fake_stateful_.Append("unencrypted/import_extensions/extensions");
    ASSERT_TRUE(base::CreateDirectory(extensions));
    ASSERT_TRUE(WriteFile(extensions.Append("fileA.crx"), ""));
    ASSERT_TRUE(WriteFile(extensions.Append("fileB.crx"), ""));
    ASSERT_TRUE(WriteFile(extensions.Append("fileC.tar"), ""));
    ASSERT_TRUE(WriteFile(extensions.Append("fileD.bmp"), ""));
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

  CrosSystemFake* cros_system_;
  ClobberState clobber_;
  base::ScopedTempDir temp_dir_;
  base::FilePath fake_stateful_;
};

TEST_F(GetPreservedFilesListTest, NoOptions) {
  ASSERT_TRUE(cros_system_->SetInt(CrosSystem::kDebugBuild, 0));
  EXPECT_EQ(clobber_.GetPreservedFilesList().size(), 0);

  ASSERT_TRUE(cros_system_->SetInt(CrosSystem::kDebugBuild, 1));
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

  ASSERT_TRUE(cros_system_->SetInt(CrosSystem::kDebugBuild, 0));
  std::vector<base::FilePath> preserved_files =
      clobber_.GetPreservedFilesList();
  std::set<base::FilePath> preserved_set(preserved_files.begin(),
                                         preserved_files.end());
  std::set<std::string> expected_preserved_set{
      "unencrypted/preserve/powerwash_count",
      "unencrypted/preserve/tpm_firmware_update_request",
      "unencrypted/preserve/update_engine/prefs/rollback-happened",
      "unencrypted/preserve/update_engine/prefs/rollback-version",
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
      "unencrypted/cros-components/offline-demo-mode-resources/table"};
  SetCompare(expected_preserved_set, preserved_set);
}

TEST_F(GetPreservedFilesListTest, SafeAndRollbackWipe) {
  ClobberState::Arguments args;
  args.safe_wipe = true;
  args.rollback_wipe = true;
  clobber_.SetArgsForTest(args);
  ASSERT_TRUE(cros_system_->SetInt(CrosSystem::kDebugBuild, 0));

  std::vector<base::FilePath> preserved_files =
      clobber_.GetPreservedFilesList();
  std::set<base::FilePath> preserved_set(preserved_files.begin(),
                                         preserved_files.end());
  std::set<std::string> expected_preserved_set{
      "unencrypted/preserve/powerwash_count",
      "unencrypted/preserve/tpm_firmware_update_request",
      "unencrypted/preserve/update_engine/prefs/rollback-happened",
      "unencrypted/preserve/update_engine/prefs/rollback-version",
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
      "unencrypted/preserve/attestation.epb",
      "unencrypted/preserve/rollback_data"};
  SetCompare(expected_preserved_set, preserved_set);
}

TEST_F(GetPreservedFilesListTest, FactoryWipe) {
  ClobberState::Arguments args;
  args.factory_wipe = true;
  clobber_.SetArgsForTest(args);

  ASSERT_TRUE(cros_system_->SetInt(CrosSystem::kDebugBuild, 0));
  std::vector<base::FilePath> preserved_files =
      clobber_.GetPreservedFilesList();
  std::set<base::FilePath> preserved_set(preserved_files.begin(),
                                         preserved_files.end());
  std::set<std::string> expected_preserved_set{
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

  ASSERT_TRUE(cros_system_->SetInt(CrosSystem::kDebugBuild, 0));
  std::vector<base::FilePath> preserved_files =
      clobber_.GetPreservedFilesList();
  std::set<base::FilePath> preserved_set(preserved_files.begin(),
                                         preserved_files.end());
  std::set<std::string> expected_preserved_set{
      "unencrypted/preserve/powerwash_count",
      "unencrypted/preserve/tpm_firmware_update_request",
      "unencrypted/preserve/update_engine/prefs/rollback-happened",
      "unencrypted/preserve/update_engine/prefs/rollback-version",
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
      "unencrypted/preserve/attestation.epb",
      "unencrypted/preserve/rollback_data",
      "unencrypted/import_extensions/extensions/fileA.crx",
      "unencrypted/import_extensions/extensions/fileB.crx"};
  SetCompare(expected_preserved_set, preserved_set);
}

// Version of ClobberState with some library calls mocked for testing.
class ClobberStateMock : public ClobberState {
 public:
  ClobberStateMock(const Arguments& args,
                   std::unique_ptr<CrosSystem> cros_system,
                   std::unique_ptr<ClobberUi> ui)
      : ClobberState(args, std::move(cros_system), std::move(ui)),
        secure_erase_supported_(false) {}

  void SetStatResultForPath(const base::FilePath& path, const struct stat& st) {
    result_map_[path.value()] = st;
  }

  void SetSecureEraseSupported(bool supported) {
    secure_erase_supported_ = supported;
  }

 protected:
  int Stat(const base::FilePath& path, struct stat* st) override {
    if (st == nullptr || result_map_.count(path.value()) == 0) {
      return -1;
    }

    *st = result_map_[path.value()];
    return 0;
  }

  bool SecureErase(const base::FilePath& path) override {
    return secure_erase_supported_ && base::DeleteFile(path, false);
  }

  bool DropCaches() override { return secure_erase_supported_; }

 private:
  std::unordered_map<std::string, struct stat> result_map_;
  bool secure_erase_supported_;
};

class IsRotationalTest : public ::testing::Test {
 protected:
  IsRotationalTest()
      : clobber_(ClobberState::Arguments(),
                 std::make_unique<CrosSystemFake>(),
                 std::make_unique<ClobberUi>(DevNull())) {}

  void SetUp() override {
    ASSERT_TRUE(fake_dev_.CreateUniqueTempDir());
    ASSERT_TRUE(fake_sys_.CreateUniqueTempDir());
    clobber_.SetDevForTest(fake_dev_.GetPath());
    clobber_.SetSysForTest(fake_sys_.GetPath());
  }

  ClobberStateMock clobber_;
  base::ScopedTempDir fake_dev_;
  base::ScopedTempDir fake_sys_;
};

TEST_F(IsRotationalTest, NonExistentDevice) {
  EXPECT_FALSE(clobber_.IsRotational(fake_dev_.GetPath().Append("nvme0n1p3")));
}

TEST_F(IsRotationalTest, DeviceNotUnderDev) {
  EXPECT_FALSE(clobber_.IsRotational(fake_sys_.GetPath().Append("sdc6")));
}

TEST_F(IsRotationalTest, NoRotationalFile) {
  std::string device_name = "sdq5";
  std::string disk_name = "sdq";
  base::FilePath device = fake_dev_.GetPath().Append(device_name);
  base::FilePath disk = fake_dev_.GetPath().Append(disk_name);
  ASSERT_TRUE(WriteFile(device, ""));
  ASSERT_TRUE(WriteFile(disk, ""));

  struct stat st;
  st.st_rdev = makedev(14, 7);
  st.st_mode = S_IFBLK;
  clobber_.SetStatResultForPath(device, st);

  st.st_rdev = makedev(14, 0);
  clobber_.SetStatResultForPath(disk, st);

  EXPECT_FALSE(clobber_.IsRotational(device));
}

TEST_F(IsRotationalTest, NoMatchingBaseDevice) {
  std::string device_name = "mmcblk1p5";
  std::string disk_name = "sda";
  base::FilePath device = fake_dev_.GetPath().Append(device_name);
  base::FilePath disk = fake_dev_.GetPath().Append(disk_name);
  ASSERT_TRUE(WriteFile(device, ""));
  ASSERT_TRUE(WriteFile(disk, ""));

  struct stat st;
  st.st_rdev = makedev(5, 3);
  st.st_mode = S_IFBLK;
  clobber_.SetStatResultForPath(device, st);

  st.st_rdev = makedev(7, 0);
  clobber_.SetStatResultForPath(disk, st);

  base::FilePath rotational_file =
      fake_sys_.GetPath().Append("block").Append(disk_name).Append(
          "queue/rotational");
  ASSERT_TRUE(WriteFile(rotational_file, "1\n"));
  EXPECT_FALSE(clobber_.IsRotational(device));
}

TEST_F(IsRotationalTest, DifferentRotationalFileFormats) {
  std::string device_name = "mmcblk1p5";
  std::string disk_name = "mmcblk1";
  base::FilePath device = fake_dev_.GetPath().Append(device_name);
  base::FilePath disk = fake_dev_.GetPath().Append(disk_name);
  ASSERT_TRUE(WriteFile(device, ""));
  ASSERT_TRUE(WriteFile(disk, ""));

  struct stat st;
  st.st_rdev = makedev(5, 3);
  st.st_mode = S_IFBLK;
  clobber_.SetStatResultForPath(device, st);

  st.st_rdev = makedev(5, 0);
  clobber_.SetStatResultForPath(disk, st);

  base::FilePath rotational_file =
      fake_sys_.GetPath().Append("block").Append(disk_name).Append(
          "queue/rotational");
  ASSERT_TRUE(WriteFile(rotational_file, "0\n"));
  EXPECT_FALSE(clobber_.IsRotational(device));

  ASSERT_TRUE(WriteFile(rotational_file, "0"));
  EXPECT_FALSE(clobber_.IsRotational(device));

  ASSERT_TRUE(WriteFile(rotational_file, "aldf"));
  EXPECT_FALSE(clobber_.IsRotational(device));

  ASSERT_TRUE(WriteFile(rotational_file, "1"));
  EXPECT_TRUE(clobber_.IsRotational(device));

  ASSERT_TRUE(WriteFile(rotational_file, "1\n"));
  EXPECT_TRUE(clobber_.IsRotational(device));
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
  ASSERT_TRUE(WriteFile(device_one, ""));
  ASSERT_TRUE(WriteFile(disk_one, ""));
  ASSERT_TRUE(WriteFile(device_two, ""));
  ASSERT_TRUE(WriteFile(disk_two, ""));

  struct stat st;
  st.st_rdev = makedev(5, 5);
  st.st_mode = S_IFBLK;
  clobber_.SetStatResultForPath(device_one, st);

  st.st_rdev = makedev(5, 0);
  clobber_.SetStatResultForPath(disk_one, st);

  st.st_rdev = makedev(2, 1);
  st.st_mode = S_IFBLK;
  clobber_.SetStatResultForPath(device_two, st);

  st.st_rdev = makedev(2, 0);
  clobber_.SetStatResultForPath(disk_two, st);

  base::FilePath rotational_file_one = fake_sys_.GetPath()
                                           .Append("block")
                                           .Append(disk_name_one)
                                           .Append("queue/rotational");
  ASSERT_TRUE(WriteFile(rotational_file_one, "0\n"));

  base::FilePath rotational_file_two = fake_sys_.GetPath()
                                           .Append("block")
                                           .Append(disk_name_two)
                                           .Append("queue/rotational");
  ASSERT_TRUE(WriteFile(rotational_file_two, "1"));

  EXPECT_FALSE(clobber_.IsRotational(device_one));
  EXPECT_TRUE(clobber_.IsRotational(device_two));
}

class AttemptSwitchToFastWipeTest : public ::testing::Test {
 protected:
  AttemptSwitchToFastWipeTest()
      : clobber_(ClobberState::Arguments(),
                 std::make_unique<CrosSystemFake>(),
                 std::make_unique<ClobberUi>(DevNull())) {}

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

    keyset_paths_ = std::vector<base::FilePath>({
        fake_stateful_.Append("encrypted.key"),
        fake_stateful_.Append("encrypted.needs-finalization"),
        fake_stateful_.Append("home/.shadow/cryptohome.key"),
        fake_stateful_.Append("home/.shadow/salt"),
        fake_stateful_.Append("home/.shadow/salt.sum"),
        fake_stateful_.Append("home/.shadow/random_dir/master"),
        fake_stateful_.Append("home/.shadow/other_dir/master"),
        fake_stateful_.Append("home/.shadow/extra_dir/master"),
    });

    shredded_paths_ = std::vector<base::FilePath>(
        {fake_stateful_.Append("really/deeply/buried/random/file/to/delete"),
         fake_stateful_.Append("other/file/to/delete")});

    for (const base::FilePath& path : encrypted_stateful_paths_) {
      ASSERT_TRUE(WriteFile(path, kContents));
    }

    for (const base::FilePath& path : keyset_paths_) {
      ASSERT_TRUE(WriteFile(path, kContents));
    }

    for (const base::FilePath& path : shredded_paths_) {
      ASSERT_TRUE(WriteFile(path, kContents));
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

  ClobberStateMock clobber_;
  base::ScopedTempDir temp_dir_;
  base::FilePath temp_path_;
  base::FilePath fake_stateful_;
  // Files which are deleted by ShredRotationalStatefulPaths.
  std::vector<base::FilePath> encrypted_stateful_paths_;
  // Files which are deleted by WipeKeysets.
  std::vector<base::FilePath> keyset_paths_;
  // Files which will be shredded (overwritten) but not deleted by
  // ShredRotationalStatefulPaths.
  std::vector<base::FilePath> shredded_paths_;
};

TEST_F(AttemptSwitchToFastWipeTest, NotRotationalNoSecureErase) {
  ClobberState::Arguments args;
  args.fast_wipe = false;
  clobber_.SetArgsForTest(args);

  clobber_.SetSecureEraseSupported(false);
  clobber_.AttemptSwitchToFastWipe(false);
  EXPECT_FALSE(clobber_.GetArgsForTest().fast_wipe);
  CheckPathsUntouched(encrypted_stateful_paths_);
  CheckPathsUntouched(keyset_paths_);
  CheckPathsUntouched(shredded_paths_);
}

TEST_F(AttemptSwitchToFastWipeTest, AlreadyFast) {
  ClobberState::Arguments args;
  args.fast_wipe = true;
  clobber_.SetArgsForTest(args);

  clobber_.SetSecureEraseSupported(true);
  clobber_.AttemptSwitchToFastWipe(true);
  EXPECT_TRUE(clobber_.GetArgsForTest().fast_wipe);
  CheckPathsUntouched(encrypted_stateful_paths_);
  CheckPathsUntouched(keyset_paths_);
  CheckPathsUntouched(shredded_paths_);
}

TEST_F(AttemptSwitchToFastWipeTest, RotationalNoSecureErase) {
  ClobberState::Arguments args;
  args.fast_wipe = false;
  clobber_.SetArgsForTest(args);

  clobber_.SetSecureEraseSupported(false);
  clobber_.AttemptSwitchToFastWipe(true);
  EXPECT_TRUE(clobber_.GetArgsForTest().fast_wipe);
  CheckPathsDeleted(encrypted_stateful_paths_);
  CheckPathsShredded(keyset_paths_);
  CheckPathsShredded(shredded_paths_);
}

TEST_F(AttemptSwitchToFastWipeTest, SecureEraseNotRotational) {
  ClobberState::Arguments args;
  args.fast_wipe = false;
  clobber_.SetArgsForTest(args);

  clobber_.SetSecureEraseSupported(true);
  clobber_.AttemptSwitchToFastWipe(false);
  EXPECT_TRUE(clobber_.GetArgsForTest().fast_wipe);
  CheckPathsUntouched(encrypted_stateful_paths_);
  CheckPathsDeleted(keyset_paths_);
  CheckPathsUntouched(shredded_paths_);
}

TEST_F(AttemptSwitchToFastWipeTest, SecureEraseNotRotationalFactoryWipe) {
  ClobberState::Arguments args;
  args.fast_wipe = false;
  args.factory_wipe = true;
  clobber_.SetArgsForTest(args);

  clobber_.SetSecureEraseSupported(true);
  clobber_.AttemptSwitchToFastWipe(false);
  EXPECT_TRUE(clobber_.GetArgsForTest().fast_wipe);
  CheckPathsUntouched(encrypted_stateful_paths_);
  CheckPathsDeleted(keyset_paths_);
  CheckPathsUntouched(shredded_paths_);
}

TEST_F(AttemptSwitchToFastWipeTest, RotationalSecureErase) {
  ClobberState::Arguments args;
  args.fast_wipe = false;
  clobber_.SetArgsForTest(args);

  clobber_.SetSecureEraseSupported(true);
  clobber_.AttemptSwitchToFastWipe(true);
  EXPECT_TRUE(clobber_.GetArgsForTest().fast_wipe);
  CheckPathsDeleted(encrypted_stateful_paths_);
  CheckPathsShredded(keyset_paths_);
  CheckPathsShredded(shredded_paths_);
}

TEST_F(AttemptSwitchToFastWipeTest, RotationalSecureEraseFactoryWipe) {
  ClobberState::Arguments args;
  args.fast_wipe = false;
  args.factory_wipe = true;
  clobber_.SetArgsForTest(args);

  clobber_.SetSecureEraseSupported(true);
  clobber_.AttemptSwitchToFastWipe(true);
  EXPECT_TRUE(clobber_.GetArgsForTest().fast_wipe);
  CheckPathsDeleted(encrypted_stateful_paths_);
  CheckPathsShredded(keyset_paths_);
  CheckPathsShredded(shredded_paths_);
}

class ShredRotationalStatefulFilesTest : public ::testing::Test {
 protected:
  ShredRotationalStatefulFilesTest()
      : clobber_(ClobberState::Arguments(),
                 std::make_unique<CrosSystemFake>(),
                 std::make_unique<ClobberUi>(DevNull())) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    temp_path_ = temp_dir_.GetPath();
    fake_stateful_ = temp_path_.Append("stateful");
    clobber_.SetStatefulForTest(fake_stateful_);

    base::FilePath shadow = fake_stateful_.Append("home/.shadow");

    deleted_paths_ = std::vector<base::FilePath>({
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

    shredded_paths_ = std::vector<base::FilePath>(
        {fake_stateful_.Append("really/deeply/buried/random/file/to/delete"),
         fake_stateful_.Append("other/file/to/delete")});

    for (const base::FilePath& path : deleted_paths_) {
      ASSERT_TRUE(WriteFile(path, kContents));
    }

    for (const base::FilePath& path : shredded_paths_) {
      ASSERT_TRUE(WriteFile(path, kContents));
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

  ClobberStateMock clobber_;
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

class WipeKeysetsTest : public ::testing::Test {
 protected:
  WipeKeysetsTest()
      : clobber_(ClobberState::Arguments(),
                 std::make_unique<CrosSystemFake>(),
                 std::make_unique<ClobberUi>(DevNull())) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    fake_stateful_ = temp_dir_.GetPath();
    clobber_.SetStatefulForTest(fake_stateful_);

    deleted_paths_ = std::vector<base::FilePath>({
        fake_stateful_.Append("encrypted.key"),
        fake_stateful_.Append("encrypted.needs-finalization"),
        fake_stateful_.Append("home/.shadow/cryptohome.key"),
        fake_stateful_.Append("home/.shadow/salt"),
        fake_stateful_.Append("home/.shadow/salt.sum"),
        fake_stateful_.Append("home/.shadow/random_dir/master"),
        fake_stateful_.Append("home/.shadow/other_dir/master"),
        fake_stateful_.Append("home/.shadow/extra_dir/master"),
    });

    ignored_paths_ = std::vector<base::FilePath>({
        fake_stateful_.Append("uninteresting/file/definitely/not/an/rsa/key"),
        fake_stateful_.Append("hopefully/not/a/copy/of/etc/passwd"),
        fake_stateful_.Append("home/.shadow/extra_dir/unimportant"),
        fake_stateful_.Append("home/.shadow/other_dir/unimportant"),
    });

    for (const base::FilePath& path : deleted_paths_) {
      ASSERT_TRUE(WriteFile(path, kContents));
    }

    for (const base::FilePath& path : ignored_paths_) {
      ASSERT_TRUE(WriteFile(path, kContents));
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

  ClobberStateMock clobber_;
  base::ScopedTempDir temp_dir_;
  base::FilePath fake_stateful_;
  std::vector<base::FilePath> deleted_paths_;
  std::vector<base::FilePath> ignored_paths_;
};

TEST_F(WipeKeysetsTest, NotSupported) {
  clobber_.SetSecureEraseSupported(false);
  CheckPathsUntouched(deleted_paths_);
  CheckPathsUntouched(ignored_paths_);

  EXPECT_FALSE(clobber_.WipeKeysets());

  CheckPathsUntouched(ignored_paths_);
}

TEST_F(WipeKeysetsTest, Supported) {
  clobber_.SetSecureEraseSupported(true);
  CheckPathsUntouched(deleted_paths_);
  CheckPathsUntouched(ignored_paths_);

  EXPECT_TRUE(clobber_.WipeKeysets());

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

  ClobberState::PartitionNumbers partitions_;
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
  EXPECT_EQ(wipe_info.stateful_device.value(), "/dev/mmcblk0p1");
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
  EXPECT_EQ(wipe_info.stateful_device.value(), "/dev/nvme0n1p1");
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
  EXPECT_EQ(wipe_info.stateful_device.value(), "/dev/nvme0n1p1");
  EXPECT_EQ(wipe_info.inactive_root_device.value(), "/dev/nvme0n1p3");
  EXPECT_EQ(wipe_info.inactive_kernel_device.value(), "/dev/nvme0n1p2");
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
  EXPECT_EQ(wipe_info.stateful_device.value(), "/dev/sda7");
  EXPECT_EQ(wipe_info.inactive_root_device.value(), "/dev/sda4");
  EXPECT_EQ(wipe_info.inactive_kernel_device.value(), "/dev/sda2");
  EXPECT_FALSE(wipe_info.is_mtd_flash);
  EXPECT_EQ(wipe_info.active_kernel_partition, partitions_.kernel_a);
}

TEST(WipeBlockDevice, Nonexistent) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath file_system_path = temp_dir.GetPath().Append("fs");
  ClobberUi ui(DevNull());

  EXPECT_FALSE(ClobberState::WipeBlockDevice(file_system_path, &ui, false));
  EXPECT_FALSE(ClobberState::WipeBlockDevice(file_system_path, &ui, true));
}

TEST(WipeBlockDevice, Fast) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath device_path = temp_dir.GetPath().Append("device");
  base::File device(device_path,
                    base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(device.IsValid());
  size_t buf_size = 1024 * 4;
  size_t num_blocks = 3;
  size_t block_size = 1024 * 1024 * 4;
  size_t device_size = num_blocks * block_size;
  ASSERT_TRUE(device.SetLength(device_size));
  std::vector<char> write_buf;
  write_buf.resize(buf_size, 'F');
  std::vector<size_t> offsets{0, 52 * buf_size, 107 * buf_size,
                              block_size + buf_size};
  for (size_t offset : offsets) {
    ASSERT_LE(offset, device_size - buf_size);
    ASSERT_EQ(device.Write(offset, &write_buf[0], buf_size), buf_size);
  }
  device.Close();

  ClobberUi ui(DevNull());
  EXPECT_TRUE(ClobberState::WipeBlockDevice(device_path, &ui, true));

  device =
      base::File(device_path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  EXPECT_TRUE(device.IsValid());
  EXPECT_EQ(device.GetLength(), device_size);
  std::vector<char> zero_buf;
  zero_buf.resize(buf_size, '\0');
  std::vector<char> read_buf;
  read_buf.resize(buf_size);

  for (size_t offset : offsets) {
    ASSERT_LE(offset, device_size - buf_size);
    EXPECT_EQ(device.Read(offset, &read_buf[0], buf_size), buf_size)
        << "Could not read at offset " << offset;
    if (offset + buf_size <= block_size) {
      EXPECT_EQ(read_buf, zero_buf);
    } else if (offset >= block_size) {
      EXPECT_EQ(read_buf, write_buf);
    }
  }
}

TEST(WipeBlockDevice, Slow) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath file_system_path = temp_dir.GetPath().Append("fs");
  base::File file_system(file_system_path,
                         base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  size_t file_system_size = 9.5 * 1024 * 1024;
  ASSERT_TRUE(file_system.IsValid());
  ASSERT_TRUE(file_system.SetLength(file_system_size));

  size_t buf_size = 1024 * 4;
  std::vector<char> buf;
  buf.resize(buf_size, 'F');
  std::vector<size_t> offsets{0, 500 * buf_size, 783 * buf_size,
                              file_system_size - buf_size};
  for (size_t offset : offsets) {
    ASSERT_LE(offset, file_system_size - buf_size);
    ASSERT_EQ(file_system.Write(offset, &buf[0], buf_size), buf_size);
  }

  ASSERT_TRUE(file_system.Flush());
  file_system.Close();

  brillo::ProcessImpl mkfs;
  mkfs.AddArg("/sbin/mkfs.ext4");
  mkfs.AddArg(file_system_path.value());
  EXPECT_EQ(mkfs.Run(), 0);

  ClobberUi ui(DevNull());
  EXPECT_TRUE(ClobberState::WipeBlockDevice(file_system_path, &ui, false));

  file_system = base::File(file_system_path,
                           base::File::FLAG_OPEN | base::File::FLAG_READ);
  std::vector<char> zero_buf;
  zero_buf.resize(buf_size, '\0');
  for (size_t offset : offsets) {
    ASSERT_LE(offset, file_system_size - buf_size);
    EXPECT_EQ(file_system.Read(offset, &buf[0], buf_size), buf_size);
    EXPECT_EQ(buf, zero_buf);
  }
}
