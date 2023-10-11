// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/clobber/clobber_lvm.h"

#include <memory>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/blkdev_utils/mock_lvm.h>
#include <gtest/gtest.h>
#include <libdlcservice/mock_utils.h>
#include <libdlcservice/utils.h>

#include "gmock/gmock.h"

#include "init/clobber/clobber_wipe_mock.h"

namespace {
using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::StrictMock;
}  // namespace

constexpr char kPhysicalVolumeReport[] =
    "{\"report\": [{ \"pv\": [ {\"pv_name\":\"/dev/mmcblk0p1\", "
    "\"vg_name\":\"stateful\"}]}]}";
constexpr char kThinpoolReport[] =
    "{\"report\": [{ \"lv\": [ {\"lv_name\":\"thinpool\", "
    "\"vg_name\":\"stateful\"}]}]}";
constexpr char kLogicalVolumeReport[] =
    "{\"report\": [{ \"lv\": [ {\"lv_name\":\"unencrypted\", "
    "\"vg_name\":\"stateful\"}]}]}";

// Version of ClobberLvm with some library calls mocked for testing.
class ClobberLvmMock : public ClobberLvm {
 public:
  ClobberLvmMock(ClobberWipeMock* wipe,
                 std::unique_ptr<brillo::LogicalVolumeManager> lvm)
      : ClobberLvm(wipe, std::move(lvm)) {}

 protected:
  uint64_t GetBlkSize(const base::FilePath& device) override {
    return stateful_partition_size_;
  }

  std::string GenerateRandomVolumeGroupName() override {
    return "STATEFULSTATEFUL";
  }

 private:
  uint64_t stateful_partition_size_ = 5ULL * 1024 * 1024 * 1024;
};

class LogicalVolumeStatefulPartitionTest : public ::testing::Test {
 public:
  LogicalVolumeStatefulPartitionTest()
      : lvm_command_runner_(std::make_shared<brillo::MockLvmCommandRunner>()),
        clobber_lvm_(nullptr,
                     std::make_unique<brillo::LogicalVolumeManager>(
                         lvm_command_runner_)) {}
  ~LogicalVolumeStatefulPartitionTest() = default;

  void ExpectStatefulLogicalVolume() {
    // Expect physical volume and volume group.
    std::vector<std::string> pvs = {"/sbin/pvs", "--reportformat", "json",
                                    "/dev/mmcblk0p1"};
    EXPECT_CALL(*lvm_command_runner_.get(), RunProcess(pvs, _))
        .WillRepeatedly(
            DoAll(SetArgPointee<1>(std::string(kPhysicalVolumeReport)),
                  Return(true)));
    // Expect thinpool.
    std::vector<std::string> thinpool_display = {
        "/sbin/lvs",      "-S",   "pool_lv=\"\"",
        "--reportformat", "json", "STATEFULSTATEFUL/thinpool"};
    EXPECT_CALL(*lvm_command_runner_.get(), RunProcess(thinpool_display, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(std::string(kThinpoolReport)),
                              Return(true)));
    // Expect logical volume.
    std::vector<std::string> lv_display = {
        "/sbin/lvs",      "-S",   "pool_lv!=\"\"",
        "--reportformat", "json", "STATEFULSTATEFUL/unencrypted"};
    EXPECT_CALL(*lvm_command_runner_.get(), RunProcess(lv_display, _))
        .WillRepeatedly(DoAll(
            SetArgPointee<1>(std::string(kLogicalVolumeReport)), Return(true)));
  }

 protected:
  std::shared_ptr<brillo::MockLvmCommandRunner> lvm_command_runner_;
  ClobberLvmMock clobber_lvm_;
};

TEST_F(LogicalVolumeStatefulPartitionTest, RemoveLogicalVolumeStackCheck) {
  ExpectStatefulLogicalVolume();

  EXPECT_CALL(
      *lvm_command_runner_.get(),
      RunCommand(std::vector<std::string>({"vgchange", "-an", "stateful"})))
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_CALL(
      *lvm_command_runner_.get(),
      RunCommand(std::vector<std::string>({"vgremove", "-f", "stateful"})))
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_CALL(*lvm_command_runner_.get(),
              RunCommand(std::vector<std::string>(
                  {"pvremove", "-ff", "/dev/mmcblk0p1"})))
      .Times(1)
      .WillOnce(Return(true));

  clobber_lvm_.RemoveLogicalVolumeStack(base::FilePath("/dev/mmcblk0p1"));
}

TEST_F(LogicalVolumeStatefulPartitionTest, CreateLogicalVolumeStackCheck) {
  std::vector<std::string> pv_create = {"pvcreate", "-ff", "--yes",
                                        "/dev/mmcblk0p1"};
  EXPECT_CALL(*lvm_command_runner_.get(), RunCommand(pv_create))
      .Times(1)
      .WillOnce(Return(true));

  std::vector<std::string> vg_create = {"vgcreate", "-p", "1",
                                        "STATEFULSTATEFUL", "/dev/mmcblk0p1"};
  EXPECT_CALL(*lvm_command_runner_.get(), RunCommand(vg_create))
      .Times(1)
      .WillOnce(Return(true));

  std::vector<std::string> tp_create = {"lvcreate", "--zero",
                                        "n",        "--size",
                                        "5017M",    "--poolmetadatasize",
                                        "50M",      "--thinpool",
                                        "thinpool", "STATEFULSTATEFUL"};
  EXPECT_CALL(*lvm_command_runner_.get(), RunCommand(tp_create))
      .Times(1)
      .WillOnce(Return(true));

  std::vector<std::string> lv_create = {"lvcreate",
                                        "--thin",
                                        "-V",
                                        "4766M",
                                        "-n",
                                        "unencrypted",
                                        "STATEFULSTATEFUL/thinpool"};
  EXPECT_CALL(*lvm_command_runner_.get(), RunCommand(lv_create))
      .Times(1)
      .WillOnce(Return(true));

  std::vector<std::string> vg_enable = {"vgchange", "-ay", "STATEFULSTATEFUL"};
  EXPECT_CALL(*lvm_command_runner_.get(), RunCommand(vg_enable))
      .Times(1)
      .WillOnce(Return(true));

  std::vector<std::string> lv_enable = {"lvchange", "-ay",
                                        "STATEFULSTATEFUL/unencrypted"};
  EXPECT_CALL(*lvm_command_runner_.get(), RunCommand(lv_enable))
      .Times(1)
      .WillOnce(Return(true));

  clobber_lvm_.CreateLogicalVolumeStack(base::FilePath("/dev/mmcblk0p1"));
}

class LogicalVolumeStatefulPartitionMockedTest : public ::testing::Test {
 public:
  LogicalVolumeStatefulPartitionMockedTest()
      : mock_lvm_(
            std::make_unique<StrictMock<brillo::MockLogicalVolumeManager>>()),
        mock_lvm_ptr_(mock_lvm_.get()),
        mock_lvm_command_runner_(
            std::make_shared<brillo::MockLvmCommandRunner>()),
        clobber_ui_(DevNull()),
        clobber_wipe_(&clobber_ui_),
        clobber_lvm_(&clobber_wipe_, std::move(mock_lvm_)) {}

  LogicalVolumeStatefulPartitionMockedTest(
      const LogicalVolumeStatefulPartitionMockedTest&) = delete;
  LogicalVolumeStatefulPartitionMockedTest& operator=(
      const LogicalVolumeStatefulPartitionMockedTest&) = delete;

 protected:
  std::unique_ptr<StrictMock<brillo::MockLogicalVolumeManager>> mock_lvm_;
  brillo::MockLogicalVolumeManager* mock_lvm_ptr_;
  std::shared_ptr<brillo::MockLvmCommandRunner> mock_lvm_command_runner_;
  ClobberUi clobber_ui_;
  ClobberWipeMock clobber_wipe_;
  ClobberLvmMock clobber_lvm_;
};

TEST_F(LogicalVolumeStatefulPartitionMockedTest,
       PreserveLogicalVolumesWipeNoPhysicalVolume) {
  std::optional<brillo::PhysicalVolume> pv;
  EXPECT_CALL(*mock_lvm_ptr_, GetPhysicalVolume(_)).WillOnce(Return(pv));
  EXPECT_FALSE(
      clobber_lvm_.PreserveLogicalVolumesWipe(base::FilePath("/nocheck"), {}));
  EXPECT_EQ(clobber_wipe_.WipeDeviceCalled(), 0);
}

TEST_F(LogicalVolumeStatefulPartitionMockedTest,
       PreserveLogicalVolumesWipeNoVolumeGroup) {
  auto pv = std::make_optional(brillo::PhysicalVolume(
      base::FilePath{"/foobar"}, mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetPhysicalVolume(base::FilePath{"/foobar"}))
      .WillOnce(Return(pv));

  std::optional<brillo::VolumeGroup> vg;
  EXPECT_CALL(*mock_lvm_ptr_, GetVolumeGroup(_)).WillOnce(Return(vg));

  EXPECT_FALSE(
      clobber_lvm_.PreserveLogicalVolumesWipe(base::FilePath("/foobar"), {}));

  EXPECT_EQ(clobber_wipe_.WipeDeviceCalled(), 0);
}

TEST_F(LogicalVolumeStatefulPartitionMockedTest,
       PreserveLogicalVolumesWipeEmptyInfoNoLvs) {
  auto pv = std::make_optional(brillo::PhysicalVolume(
      base::FilePath{"/foobar"}, mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetPhysicalVolume(_)).WillOnce(Return(pv));

  auto vg = std::make_optional(
      brillo::VolumeGroup("foobar_vg", mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetVolumeGroup(_)).WillOnce(Return(vg));

  std::vector<brillo::LogicalVolume> lvs;
  EXPECT_CALL(*mock_lvm_ptr_, ListLogicalVolumes(_, _)).WillOnce(Return(lvs));

  // Must always have unencrypted.
  EXPECT_FALSE(
      clobber_lvm_.PreserveLogicalVolumesWipe(base::FilePath("/foobar"), {}));

  EXPECT_EQ(clobber_wipe_.WipeDeviceCalled(), 0);
}

TEST_F(LogicalVolumeStatefulPartitionMockedTest,
       PreserveLogicalVolumesWipeEmptyInfo) {
  auto pv = std::make_optional(brillo::PhysicalVolume(
      base::FilePath{"/foobar"}, mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetPhysicalVolume(_)).WillOnce(Return(pv));

  auto vg = std::make_optional(
      brillo::VolumeGroup("foobar_vg", mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetVolumeGroup(_)).WillOnce(Return(vg));

  std::vector<brillo::LogicalVolume> lvs{
      brillo::LogicalVolume{"lv-name-1", "vg-name-1", mock_lvm_command_runner_},
  };
  EXPECT_CALL(*mock_lvm_ptr_, ListLogicalVolumes(_, _)).WillOnce(Return(lvs));

  EXPECT_CALL(*mock_lvm_command_runner_.get(),
              RunCommand(std::vector<std::string>{"lvremove", "--force",
                                                  lvs[0].GetName()}))
      .WillOnce(Return(true));

  EXPECT_FALSE(
      clobber_lvm_.PreserveLogicalVolumesWipe(base::FilePath("/foobar"), {}));
}

TEST_F(LogicalVolumeStatefulPartitionMockedTest,
       PreserveLogicalVolumesWipeIncludeInfoNoLvs) {
  auto pv = std::make_optional(brillo::PhysicalVolume(
      base::FilePath{"/foobar"}, mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetPhysicalVolume(_)).WillOnce(Return(pv));

  auto vg = std::make_optional(
      brillo::VolumeGroup("foobar_vg", mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetVolumeGroup(_)).WillOnce(Return(vg));

  auto lv = std::make_optional(brillo::LogicalVolume(
      kUnencrypted, vg->GetName(), mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetLogicalVolume(_, kUnencrypted))
      .WillOnce(Return(lv));

  std::vector<brillo::LogicalVolume> lvs;
  EXPECT_CALL(*mock_lvm_ptr_, ListLogicalVolumes(_, _)).WillOnce(Return(lvs));

  EXPECT_TRUE(clobber_lvm_.PreserveLogicalVolumesWipe(
      base::FilePath("/foobar"), {
                                     {
                                         .lv_name = kUnencrypted,
                                         .preserve = true,
                                         .zero = false,
                                     },
                                 }));

  EXPECT_EQ(clobber_wipe_.WipeDeviceCalled(), 0);
}

TEST_F(LogicalVolumeStatefulPartitionMockedTest,
       PreserveLogicalVolumesWipeNoInfoMatch) {
  auto pv = std::make_optional(brillo::PhysicalVolume(
      base::FilePath{"/foobar"}, mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetPhysicalVolume(_)).WillOnce(Return(pv));

  auto vg = std::make_optional(
      brillo::VolumeGroup("foobar_vg", mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetVolumeGroup(_)).WillOnce(Return(vg));

  auto lv = std::make_optional(brillo::LogicalVolume(
      kUnencrypted, vg->GetName(), mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetLogicalVolume(_, kUnencrypted))
      .WillOnce(Return(lv));

  std::vector<brillo::LogicalVolume> lvs{
      brillo::LogicalVolume{"lv-name-1", "vg-name-1", mock_lvm_command_runner_},
  };
  EXPECT_CALL(*mock_lvm_ptr_, ListLogicalVolumes(_, _)).WillOnce(Return(lvs));

  EXPECT_CALL(*mock_lvm_command_runner_.get(),
              RunCommand(std::vector<std::string>{"lvremove", "--force",
                                                  lvs[0].GetName()}))
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_lvm_command_runner_,
              RunCommand(std::vector<std::string>{"vgrename", "foobar_vg",
                                                  "STATEFULSTATEFUL"}))
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_lvm_command_runner_, RunCommand(std::vector<std::string>{
                                             "lvchange", "-ay", lv->GetName()}))
      .WillOnce(Return(true));

  EXPECT_TRUE(clobber_lvm_.PreserveLogicalVolumesWipe(
      base::FilePath("/foobar"), {
                                     {
                                         .lv_name = kUnencrypted,
                                         .preserve = true,
                                         .zero = false,
                                     },
                                 }));

  EXPECT_EQ(clobber_wipe_.WipeDeviceCalled(), 0);
}

TEST_F(LogicalVolumeStatefulPartitionMockedTest,
       PreserveLogicalVolumesWipeInfoMatchPreserve) {
  auto pv = std::make_optional(brillo::PhysicalVolume(
      base::FilePath{"/foobar"}, mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetPhysicalVolume(_)).WillOnce(Return(pv));

  auto vg = std::make_optional(
      brillo::VolumeGroup("foobar_vg", mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetVolumeGroup(_)).WillOnce(Return(vg));

  auto lv = std::make_optional(brillo::LogicalVolume(
      kUnencrypted, vg->GetName(), mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetLogicalVolume(_, kUnencrypted))
      .WillOnce(Return(lv));

  std::vector<brillo::LogicalVolume> lvs{
      brillo::LogicalVolume{kUnencrypted, "vg-name-1",
                            mock_lvm_command_runner_},
  };
  EXPECT_CALL(*mock_lvm_ptr_, ListLogicalVolumes(_, _)).WillOnce(Return(lvs));

  EXPECT_CALL(*mock_lvm_command_runner_.get(),
              RunCommand(std::vector<std::string>{"lvremove", "--force",
                                                  lvs[0].GetName()}))
      .Times(0);

  EXPECT_CALL(*mock_lvm_command_runner_,
              RunCommand(std::vector<std::string>{"vgrename", "foobar_vg",
                                                  "STATEFULSTATEFUL"}))
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_lvm_command_runner_, RunCommand(std::vector<std::string>{
                                             "lvchange", "-ay", lv->GetName()}))
      .WillOnce(Return(true));

  EXPECT_TRUE(clobber_lvm_.PreserveLogicalVolumesWipe(
      base::FilePath("/foobar"), {
                                     {
                                         .lv_name = kUnencrypted,
                                         .preserve = true,
                                         .zero = false,
                                     },
                                 }));

  EXPECT_EQ(clobber_wipe_.WipeDeviceCalled(), 0);
}

TEST_F(LogicalVolumeStatefulPartitionMockedTest,
       PreserveLogicalVolumesWipeInfoMatchZero) {
  auto pv = std::make_optional(brillo::PhysicalVolume(
      base::FilePath{"/foobar"}, mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetPhysicalVolume(_)).WillOnce(Return(pv));

  auto vg = std::make_optional(
      brillo::VolumeGroup("foobar_vg", mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetVolumeGroup(_)).WillOnce(Return(vg));

  auto lv = std::make_optional(brillo::LogicalVolume(
      kUnencrypted, vg->GetName(), mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetLogicalVolume(_, kUnencrypted))
      .WillOnce(Return(lv));

  std::vector<brillo::LogicalVolume> lvs{
      brillo::LogicalVolume{kUnencrypted, "vg-name-1",
                            mock_lvm_command_runner_},
  };
  EXPECT_CALL(*mock_lvm_ptr_, ListLogicalVolumes(_, _)).WillOnce(Return(lvs));

  EXPECT_TRUE(clobber_lvm_.PreserveLogicalVolumesWipe(
      base::FilePath("/foobar"), {
                                     {
                                         .lv_name = kUnencrypted,
                                         .preserve = false,
                                         .zero = true,
                                     },
                                 }));

  EXPECT_EQ(clobber_wipe_.WipeDeviceCalled(), 1);
}

TEST_F(LogicalVolumeStatefulPartitionMockedTest,
       PreserveLogicalVolumesWipeInfoMatchPreserveAndZero) {
  auto pv = std::make_optional(brillo::PhysicalVolume(
      base::FilePath{"/foobar"}, mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetPhysicalVolume(_)).WillOnce(Return(pv));

  auto vg = std::make_optional(
      brillo::VolumeGroup("foobar_vg", mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetVolumeGroup(_)).WillOnce(Return(vg));

  auto lv = std::make_optional(brillo::LogicalVolume(
      kUnencrypted, vg->GetName(), mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetLogicalVolume(_, kUnencrypted))
      .WillOnce(Return(lv));

  std::vector<brillo::LogicalVolume> lvs{
      brillo::LogicalVolume{kUnencrypted, "vg-name-1",
                            mock_lvm_command_runner_},
  };
  EXPECT_CALL(*mock_lvm_ptr_, ListLogicalVolumes(_, _)).WillOnce(Return(lvs));

  EXPECT_TRUE(clobber_lvm_.PreserveLogicalVolumesWipe(
      base::FilePath("/foobar"), {
                                     {
                                         .lv_name = kUnencrypted,
                                         .preserve = true,
                                         .zero = true,
                                     },
                                 }));

  EXPECT_EQ(clobber_wipe_.WipeDeviceCalled(), 1);
}

TEST_F(LogicalVolumeStatefulPartitionMockedTest,
       PreserveLogicalVolumesWipeInfoMatchPreserveAndZeroWithNoMatchLv) {
  auto pv = std::make_optional(brillo::PhysicalVolume(
      base::FilePath{"/foobar"}, mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetPhysicalVolume(_)).WillOnce(Return(pv));

  auto vg = std::make_optional(
      brillo::VolumeGroup("foobar_vg", mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetVolumeGroup(_)).WillOnce(Return(vg));

  auto lv = std::make_optional(brillo::LogicalVolume(
      kUnencrypted, vg->GetName(), mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetLogicalVolume(_, kUnencrypted))
      .WillOnce(Return(lv));

  std::vector<brillo::LogicalVolume> lvs{
      brillo::LogicalVolume{"foobar", "vg-name-1", mock_lvm_command_runner_},
      brillo::LogicalVolume{kThinpool, "vg-name-1", mock_lvm_command_runner_},
  };
  EXPECT_CALL(*mock_lvm_ptr_, ListLogicalVolumes(_, _)).WillOnce(Return(lvs));

  for (const auto& lv : lvs) {
    EXPECT_CALL(*mock_lvm_command_runner_.get(),
                RunCommand(std::vector<std::string>{"lvremove", "--force",
                                                    lv.GetName()}))
        .WillOnce(Return(true));
  }

  EXPECT_CALL(*mock_lvm_command_runner_,
              RunCommand(std::vector<std::string>{"vgrename", "foobar_vg",
                                                  "STATEFULSTATEFUL"}))
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_lvm_command_runner_, RunCommand(std::vector<std::string>{
                                             "lvchange", "-ay", lv->GetName()}))
      .WillOnce(Return(true));

  EXPECT_TRUE(clobber_lvm_.PreserveLogicalVolumesWipe(
      base::FilePath("/foobar"), {
                                     {
                                         .lv_name = kUnencrypted,
                                         .preserve = true,
                                         .zero = true,
                                     },
                                 }));

  EXPECT_EQ(clobber_wipe_.WipeDeviceCalled(), 1);
}

class ProcessInfoTest : public ::testing::Test {
 public:
  ProcessInfoTest()
      : mock_lvm_command_runner_(
            std::make_shared<brillo::MockLvmCommandRunner>()),
        mock_lvm_(
            std::make_unique<StrictMock<brillo::MockLogicalVolumeManager>>()),
        mock_lvm_ptr_(mock_lvm_.get()),
        clobber_lvm_(nullptr, std::move(mock_lvm_)),
        mock_utils_(std::make_unique<dlcservice::MockUtils>()),
        mock_utils_ptr_(mock_utils_.get()) {}

  ProcessInfoTest(const ProcessInfoTest&) = delete;
  ProcessInfoTest& operator=(const ProcessInfoTest&) = delete;

 protected:
  std::shared_ptr<brillo::MockLvmCommandRunner> mock_lvm_command_runner_;
  std::unique_ptr<StrictMock<brillo::MockLogicalVolumeManager>> mock_lvm_;
  brillo::MockLogicalVolumeManager* mock_lvm_ptr_;
  ClobberLvmMock clobber_lvm_;
  std::unique_ptr<dlcservice::MockUtils> mock_utils_;
  dlcservice::MockUtils* mock_utils_ptr_;
};

TEST_F(ProcessInfoTest, MissingLogicalVolume) {
  const std::string& lv_name("some-lv");
  EXPECT_CALL(*mock_lvm_ptr_, GetLogicalVolume(_, lv_name))
      .WillOnce(Return(std::nullopt));
  EXPECT_TRUE(clobber_lvm_.ProcessInfo({"some-vg", nullptr},
                                       {.lv_name = lv_name}, nullptr));
}

TEST_F(ProcessInfoTest, InvalidLogicalVolume) {
  const std::string& vg_name("some-vg");
  const std::string& lv_name("");
  auto lv = std::make_optional(
      brillo::LogicalVolume(lv_name, vg_name, mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetLogicalVolume(_, lv_name))
      .WillOnce(Return(lv));
  EXPECT_TRUE(clobber_lvm_.ProcessInfo({vg_name, nullptr}, {.lv_name = lv_name},
                                       nullptr));
}

TEST_F(ProcessInfoTest, VerifyDigestInfoOfLogicalVolumeHashingFailure) {
  const std::string& vg_name("some-vg");
  const std::string& lv_name("some-lv");
  auto lv = std::make_optional(
      brillo::LogicalVolume(lv_name, vg_name, mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetLogicalVolume(_, lv_name))
      .WillOnce(Return(lv));

  int64_t bytes = 123;
  ClobberLvm::PreserveLogicalVolumesWipeInfo::DigestInfo digest_info{
      .bytes = bytes,
      .digest = {1, 2, 3},
  };

  EXPECT_CALL(*mock_utils_ptr_, HashFile(_, _, _, _)).WillOnce(Return(false));

  EXPECT_CALL(*mock_lvm_command_runner_, RunCommand(std::vector<std::string>{
                                             "lvchange", "-ay", lv->GetName()}))
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_lvm_command_runner_,
              RunCommand(std::vector<std::string>{"lvremove", "--force",
                                                  lv->GetName()}))
      .WillOnce(Return(true));

  EXPECT_TRUE(clobber_lvm_.ProcessInfo(
      {vg_name, nullptr},
      {.lv_name = lv_name, .preserve = true, .digest_info = digest_info},
      std::move(mock_utils_)));
}

TEST_F(ProcessInfoTest, VerifyDigestInfoOfLogicalVolumeHashingMismatch) {
  const std::string& vg_name("some-vg");
  const std::string& lv_name("some-lv");
  auto lv = std::make_optional(
      brillo::LogicalVolume(lv_name, vg_name, mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetLogicalVolume(_, lv_name))
      .WillOnce(Return(lv));

  int64_t bytes = 123;
  std::vector<uint8_t> digest{1, 2, 3};
  ClobberLvm::PreserveLogicalVolumesWipeInfo::DigestInfo digest_info{
      .bytes = bytes,
      .digest = digest,
  };

  EXPECT_CALL(*mock_utils_ptr_, HashFile(_, _, _, _))
      .WillOnce(DoAll(SetArgPointee<2>(std::vector<uint8_t>{}), Return(true)));

  EXPECT_CALL(*mock_lvm_command_runner_, RunCommand(std::vector<std::string>{
                                             "lvchange", "-ay", lv->GetName()}))
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_lvm_command_runner_,
              RunCommand(std::vector<std::string>{"lvremove", "--force",
                                                  lv->GetName()}))
      .WillOnce(Return(true));

  EXPECT_TRUE(clobber_lvm_.ProcessInfo(
      {vg_name, nullptr},
      {.lv_name = lv_name, .preserve = true, .digest_info = digest_info},
      std::move(mock_utils_)));
}

TEST_F(ProcessInfoTest, VerifyDigestInfoOfLogicalVolume) {
  const std::string& vg_name("some-vg");
  const std::string& lv_name("some-lv");
  auto lv = std::make_optional(
      brillo::LogicalVolume(lv_name, vg_name, mock_lvm_command_runner_));
  EXPECT_CALL(*mock_lvm_ptr_, GetLogicalVolume(_, lv_name))
      .WillOnce(Return(lv));

  int64_t bytes = 123;
  ClobberLvm::PreserveLogicalVolumesWipeInfo::DigestInfo digest_info{
      .bytes = bytes,
      .digest = {1, 2, 3},
  };

  EXPECT_CALL(*mock_utils_ptr_, HashFile(_, bytes, _, _))
      .WillOnce(
          DoAll(SetArgPointee<2>(std::vector<uint8_t>{1, 2, 3}), Return(true)));

  EXPECT_CALL(*mock_lvm_command_runner_, RunCommand(std::vector<std::string>{
                                             "lvchange", "-ay", lv->GetName()}))
      .WillOnce(Return(true));

  EXPECT_CALL(*mock_lvm_command_runner_,
              RunCommand(std::vector<std::string>{"lvremove", "--force",
                                                  lv->GetName()}))
      .Times(0);

  EXPECT_TRUE(clobber_lvm_.ProcessInfo(
      {vg_name, nullptr},
      {.lv_name = lv_name, .preserve = true, .digest_info = digest_info},
      std::move(mock_utils_)));
}

class DlcPreserveLogicalVolumesWipeArgsTest : public ::testing::Test {
 public:
  DlcPreserveLogicalVolumesWipeArgsTest()
      : clobber_lvm_(nullptr, std::unique_ptr<brillo::LogicalVolumeManager>()),
        mock_utils_(std::make_unique<dlcservice::MockUtils>()),
        mock_utils_ptr_(mock_utils_.get()) {}

  DlcPreserveLogicalVolumesWipeArgsTest(
      const DlcPreserveLogicalVolumesWipeArgsTest&) = delete;
  DlcPreserveLogicalVolumesWipeArgsTest& operator=(
      const DlcPreserveLogicalVolumesWipeArgsTest&) = delete;

  void SetUp() override { ASSERT_TRUE(temp_dir_.CreateUniqueTempDir()); }

 protected:
  ClobberLvmMock clobber_lvm_;
  std::unique_ptr<dlcservice::MockUtils> mock_utils_;
  dlcservice::MockUtils* mock_utils_ptr_;
  base::ScopedTempDir temp_dir_;
};

TEST_F(DlcPreserveLogicalVolumesWipeArgsTest, MissingPowerwashFile) {
  const auto& dlcs = clobber_lvm_.DlcPreserveLogicalVolumesWipeArgs(
      temp_dir_.GetPath(), temp_dir_.GetPath(), dlcservice::PartitionSlot::A,
      nullptr);
  EXPECT_TRUE(dlcs.empty());
}

TEST_F(DlcPreserveLogicalVolumesWipeArgsTest, EmptyPowerwashFile) {
  const auto& ps_file_path = temp_dir_.GetPath().Append("psfile");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(ps_file_path, ""));
  const auto& dlcs = clobber_lvm_.DlcPreserveLogicalVolumesWipeArgs(
      ps_file_path, temp_dir_.GetPath(), dlcservice::PartitionSlot::A, nullptr);
  EXPECT_TRUE(dlcs.empty());
}

TEST_F(DlcPreserveLogicalVolumesWipeArgsTest, MismatchingPowerwashFile) {
  const auto& ps_file_path = temp_dir_.GetPath().Append("psfile");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(ps_file_path, "some-dlc"));

  auto manifest = std::make_unique<imageloader::Manifest>();
  manifest->ParseManifest(R"(
    "manifest-version": 1,
    "image-sha256-hash": "A",
    "table-sha256-hash": "B",
    "version": 1,
    "fs-type": "squashfs",
    "powerwash-safe": false
  )");
  EXPECT_CALL(*mock_utils_ptr_, GetDlcManifest(_, _, _))
      .WillOnce(Return(std::move(manifest)));
  // DO NOT USE `manifest` beyond this point.

  const auto& dlcs = clobber_lvm_.DlcPreserveLogicalVolumesWipeArgs(
      ps_file_path, temp_dir_.GetPath(), dlcservice::PartitionSlot::A,
      std::move(mock_utils_));
  // DO NOT USE `mock_utils_` beyond this point.

  EXPECT_TRUE(dlcs.empty());
}

TEST_F(DlcPreserveLogicalVolumesWipeArgsTest, SingleDlcPowerwashFile) {
  const std::string& dlc("some-dlc");
  const auto& ps_file_path = temp_dir_.GetPath().Append("psfile");
  ASSERT_TRUE(CreateDirectoryAndWriteFile(ps_file_path, dlc));

  auto manifest = std::make_unique<imageloader::Manifest>();
  const base::Value::Dict manifest_dict =
      base::Value::Dict()
          .Set("powerwash-safe", true)
          .Set("image-sha256-hash",
               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")
          .Set("table-sha256-hash",
               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
               "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")
          .Set("version", "1")
          .Set("manifest-version", 1);
  ASSERT_TRUE(manifest->ParseManifest(manifest_dict));
  EXPECT_CALL(*mock_utils_ptr_, GetDlcManifest(temp_dir_.GetPath(), dlc, _))
      .WillOnce(Return(std::move(manifest)));
  // DO NOT USE `manifest` beyond this point.

  const auto& active_slot = dlcservice::PartitionSlot::A;
  const auto& inactive_slot = dlcservice::PartitionSlot::B;
  const auto& dlc_active_lv_name =
      dlcservice::LogicalVolumeName(dlc, active_slot);
  const auto& dlc_inactive_lv_name =
      dlcservice::LogicalVolumeName(dlc, inactive_slot);

  EXPECT_CALL(*mock_utils_ptr_, LogicalVolumeName(dlc, active_slot))
      .WillOnce(Return(dlc_active_lv_name));
  EXPECT_CALL(*mock_utils_ptr_, LogicalVolumeName(dlc, inactive_slot))
      .WillOnce(Return(dlc_inactive_lv_name));

  const auto& dlcs = clobber_lvm_.DlcPreserveLogicalVolumesWipeArgs(
      ps_file_path, temp_dir_.GetPath(), active_slot, std::move(mock_utils_));
  // DO NOT USE `mock_utils_` beyond this point.

  ASSERT_EQ(dlcs.size(), 2);

  const auto& active_iter = dlcs.find({.lv_name = dlc_active_lv_name});
  EXPECT_NE(active_iter, dlcs.end());
  const auto& inactive_iter = dlcs.find({.lv_name = dlc_inactive_lv_name});
  EXPECT_NE(inactive_iter, dlcs.end());

  EXPECT_TRUE(active_iter->preserve);
  EXPECT_TRUE(inactive_iter->preserve);

  EXPECT_FALSE(active_iter->zero);
  EXPECT_TRUE(inactive_iter->zero);
}

TEST_F(DlcPreserveLogicalVolumesWipeArgsTest, MixedDlcPowerwashFile) {
  const auto& ps_file_path = temp_dir_.GetPath().Append("psfile");
  ASSERT_TRUE(
      CreateDirectoryAndWriteFile(ps_file_path, "some-dlc\nid-ps\nid-not-ps"));

  auto manifest_ps = std::make_unique<imageloader::Manifest>();
  {
    const base::Value::Dict manifest_dict =
        base::Value::Dict()
            .Set("powerwash-safe", true)
            .Set("image-sha256-hash",
                 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")
            .Set("table-sha256-hash",
                 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")
            .Set("version", "1")
            .Set("manifest-version", 1);
    ASSERT_TRUE(manifest_ps->ParseManifest(manifest_dict));
  }
  auto manifest_not_ps = std::make_unique<imageloader::Manifest>();
  {
    const base::Value::Dict manifest_dict =
        base::Value::Dict()
            .Set("powerwash-safe", false)
            .Set("image-sha256-hash",
                 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")
            .Set("table-sha256-hash",
                 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                 "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")
            .Set("version", "1")
            .Set("manifest-version", 1);
    ASSERT_TRUE(manifest_not_ps->ParseManifest(manifest_dict));
  }

  const std::string& dlc_ps("id-ps");
  EXPECT_CALL(*mock_utils_ptr_, GetDlcManifest(temp_dir_.GetPath(), dlc_ps, _))
      .WillOnce(Return(std::move(manifest_ps)));
  EXPECT_CALL(*mock_utils_ptr_,
              GetDlcManifest(temp_dir_.GetPath(), "id-not-ps", _))
      .WillOnce(Return(std::move(manifest_not_ps)));
  EXPECT_CALL(*mock_utils_ptr_,
              GetDlcManifest(temp_dir_.GetPath(), "some-dlc", _))
      .WillOnce(Return(std::make_unique<imageloader::Manifest>()));
  // DO NOT USE `manifest_*` beyond this point.

  const auto& active_slot = dlcservice::PartitionSlot::A;
  const auto& inactive_slot = dlcservice::PartitionSlot::B;
  const auto& dlc_active_lv_name =
      dlcservice::LogicalVolumeName(dlc_ps, active_slot);
  const auto& dlc_inactive_lv_name =
      dlcservice::LogicalVolumeName(dlc_ps, inactive_slot);

  EXPECT_CALL(*mock_utils_ptr_, LogicalVolumeName(dlc_ps, active_slot))
      .WillOnce(Return(dlc_active_lv_name));
  EXPECT_CALL(*mock_utils_ptr_, LogicalVolumeName(dlc_ps, inactive_slot))
      .WillOnce(Return(dlc_inactive_lv_name));

  const auto& dlcs = clobber_lvm_.DlcPreserveLogicalVolumesWipeArgs(
      ps_file_path, temp_dir_.GetPath(), active_slot, std::move(mock_utils_));
  // DO NOT USE `mock_utils_` beyond this point.

  ASSERT_EQ(dlcs.size(), 2);

  const auto& active_iter = dlcs.find({.lv_name = dlc_active_lv_name});
  EXPECT_NE(active_iter, dlcs.end());
  const auto& inactive_iter = dlcs.find({.lv_name = dlc_inactive_lv_name});
  EXPECT_NE(inactive_iter, dlcs.end());

  EXPECT_TRUE(active_iter->preserve);
  EXPECT_TRUE(inactive_iter->preserve);

  EXPECT_FALSE(active_iter->zero);
  EXPECT_TRUE(inactive_iter->zero);
}
