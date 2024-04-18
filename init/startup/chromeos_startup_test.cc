// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <deque>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/values.h>
#include <gtest/gtest.h>
#include <libcrossystem/crossystem_fake.h>
#include <libstorage/platform/fake_platform.h>
#include <libstorage/platform/mock_platform.h>
#include <libhwsec-foundation/tlcl_wrapper/mock_tlcl_wrapper.h>
#include <vpd/fake_vpd.h>

#include "init/startup/chromeos_startup.h"
#include "init/startup/constants.h"
#include "init/startup/factory_mode_mount_helper.h"
#include "init/startup/fake_startup_dep_impl.h"
#include "init/startup/flags.h"
#include "init/startup/mock_startup_dep_impl.h"
#include "init/startup/mount_helper.h"
#include "init/startup/mount_helper_factory.h"
#include "init/startup/standard_mount_helper.h"
#include "init/startup/startup_dep_impl.h"
#include "init/startup/test_mode_mount_helper.h"

using testing::_;
using testing::AnyNumber;
using testing::ByMove;
using testing::DoAll;
using testing::HasSubstr;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace startup {

namespace {

constexpr char kTpmFirmwareUpdateCleanup[] =
    "usr/sbin/tpm-firmware-update-cleanup";
constexpr char kTpmFirmwareUpdateRequestFlagFile[] =
    "unencrypted/preserve/tpm_firmware_update_request";
constexpr char kLsbRelease[] = "etc/lsb-release";
constexpr char kStatefulPartition[] = "mnt/stateful_partition";
constexpr char kProcCmdLine[] = "proc/cmdline";
constexpr char kSysKeyLog[] = "run/create_system_key.log";
constexpr char kMntOptionsFile[] =
    "dev_image/factory/init/encstateful_mount_option";
constexpr char kLSMDir[] =
    "sys/kernel/security/chromiumos/inode_security_policies";

void RestoreconTestFunc(libstorage::Platform* platform,
                        const base::FilePath& path,
                        const std::vector<base::FilePath>& exclude,
                        bool is_recursive,
                        bool set_digests) {
  for (auto excl : exclude) {
    base::FilePath ex_file = excl.Append("exclude");
    platform->WriteStringToFile(ex_file, "exclude");
  }
  base::FilePath restore = path.Append("restore");
  platform->WriteStringToFile(restore, "restore");
}

}  // namespace

class EarlySetupTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    kernel_debug_ = base_dir_.Append("sys/kernel/debug");
    kernel_config_ = base_dir_.Append("sys/kernel/config");
    kernel_tracing_ = base_dir_.Append("sys/kernel/tracing");
    security_hardening_ = base_dir_.Append(
        "usr/share/cros/startup/disable_stateful_security_hardening");
    kernel_security_ = base_dir_.Append("sys/kernel/security");
    fs_bpf_ = base_dir_.Append("sys/fs/bpf");
    namespaces_ = base_dir_.Append("run/namespaces");
    platform_ = std::make_unique<libstorage::MockPlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    std::unique_ptr<hwsec_foundation::MockTlclWrapper> tlcl =
        std::make_unique<hwsec_foundation::MockTlclWrapper>();
    tlcl_ = tlcl.get();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::unique_ptr<vpd::Vpd>(), flags_, base_dir_, base_dir_, base_dir_,
        platform_.get(), startup_dep_.get(),
        std::make_unique<startup::StandardMountHelper>(
            platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
            true),
        std::move(tlcl));
  }

  startup::Flags flags_;
  std::unique_ptr<libstorage::MockPlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  hwsec_foundation::MockTlclWrapper* tlcl_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  base::FilePath kernel_debug_;
  base::FilePath kernel_tracing_;
  base::FilePath dev_pts_;
  base::FilePath dev_shm_;
  base::FilePath kernel_config_;
  base::FilePath security_hardening_;
  base::FilePath kernel_security_;
  base::FilePath fs_bpf_;
  base::FilePath namespaces_;
};

TEST_F(EarlySetupTest, EarlySetup) {
  // Part of the root image.
  ASSERT_TRUE(platform_->CreateDirectory(kernel_config_));

  // Check all the mounts happen.
  EXPECT_CALL(*platform_,
              Mount(base::FilePath(), kernel_debug_, "debugfs", _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              Mount(base::FilePath(), kernel_tracing_, "tracefs", _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              Mount(base::FilePath(), kernel_config_, "configfs", _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              Mount(base::FilePath(), kernel_security_, "securityfs", _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, Mount(base::FilePath(), fs_bpf_, "bpf", _, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, Mount(namespaces_, namespaces_, "", MS_BIND, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              Mount(base::FilePath(), namespaces_, "", MS_PRIVATE, _))
      .WillOnce(Return(true));

  startup_->EarlySetup();
}

class DevCheckBlockTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    auto vpd_fake = std::make_unique<vpd::FakeVpd>();
    vpd_ = vpd_fake.get();
    base_dir_ = temp_dir_.GetPath();
    dev_mode_file = base_dir_.Append(".developer_mode");
    platform_ = std::make_unique<libstorage::FakePlatform>();
    crossystem_ = platform_->GetCrosssystem();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    std::unique_ptr<hwsec_foundation::MockTlclWrapper> tlcl =
        std::make_unique<hwsec_foundation::MockTlclWrapper>();
    tlcl_ = tlcl.get();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::make_unique<vpd::Vpd>(std::move(vpd_fake)), flags_, base_dir_,
        base_dir_, base_dir_, platform_.get(), startup_dep_.get(),
        std::make_unique<startup::StandardMountHelper>(
            platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
            true),
        std::move(tlcl));
    ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("cros_debug", 1));
    platform_->CreateDirectory(dev_mode_file.DirName());
    startup_->SetDevMode(true);
  }

  crossystem::Crossystem* crossystem_;
  vpd::FakeVpd* vpd_;
  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  base::FilePath dev_mode_file;
  std::unique_ptr<libstorage::FakePlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  hwsec_foundation::MockTlclWrapper* tlcl_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
};

TEST_F(DevCheckBlockTest, DevSWBoot) {
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("devsw_boot", 0));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("debug_build", 0));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("recovery_reason", 0));
  ASSERT_TRUE(vpd_->WriteValues(vpd::VpdRw, {{"block_devmode", "1"}}));

  startup_->DevCheckBlockDevMode(dev_mode_file);
  EXPECT_FALSE(platform_->FileExists(dev_mode_file));
}

TEST_F(DevCheckBlockTest, VpdCrosSysBlockDev) {
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("devsw_boot", 1));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("debug_build", 0));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("recovery_reason", 0));
  ASSERT_TRUE(vpd_->WriteValues(vpd::VpdRw, {{"block_devmode", "0"}}));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("block_devmode", 1));

  startup_->DevCheckBlockDevMode(dev_mode_file);
  EXPECT_TRUE(platform_->FileExists(dev_mode_file));
}

TEST_F(DevCheckBlockTest, CrosSysBlockDev) {
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("devsw_boot", 1));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("debug_build", 0));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("recovery_reason", 0));
  // No "block_devmode" in VPD.
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("block_devmode", 1));

  startup_->DevCheckBlockDevMode(dev_mode_file);
  EXPECT_TRUE(platform_->FileExists(dev_mode_file));
}

class TPMTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    platform_ = std::make_unique<libstorage::MockPlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    std::unique_ptr<hwsec_foundation::MockTlclWrapper> tlcl =
        std::make_unique<hwsec_foundation::MockTlclWrapper>();
    tlcl_ = tlcl.get();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::make_unique<vpd::Vpd>(), flags_, base_dir_, base_dir_, base_dir_,
        platform_.get(), startup_dep_.get(),
        std::make_unique<startup::StandardMountHelper>(
            platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
            false),
        std::move(tlcl));
  }

  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  std::unique_ptr<libstorage::MockPlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  hwsec_foundation::MockTlclWrapper* tlcl_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
};

TEST_F(TPMTest, OwnedTrue) {
  EXPECT_CALL(*tlcl_, Init()).WillOnce(Return(0));
  EXPECT_CALL(*tlcl_, GetOwnership(_))
      .WillOnce(DoAll(SetArgPointee<0>(true), Return(0)));
  EXPECT_CALL(*tlcl_, Close()).WillOnce(Return(0));
  EXPECT_TRUE(startup_->IsTPMOwned());
}

TEST_F(TPMTest, OwnedFalse) {
  EXPECT_CALL(*tlcl_, Init()).WillOnce(Return(0));
  EXPECT_CALL(*tlcl_, GetOwnership(_))
      .WillOnce(DoAll(SetArgPointee<0>(false), Return(0)));
  EXPECT_CALL(*tlcl_, Close()).WillOnce(Return(0));
  EXPECT_FALSE(startup_->IsTPMOwned());
}

TEST_F(TPMTest, OwnedUnknown) {
  EXPECT_CALL(*tlcl_, Init()).WillOnce(Return(0));
  EXPECT_CALL(*tlcl_, GetOwnership(_)).WillOnce(Return(1));
  EXPECT_CALL(*tlcl_, Close()).WillOnce(Return(0));
  EXPECT_TRUE(startup_->IsTPMOwned());
}

TEST_F(TPMTest, NeedsClobberTPMOwned) {
  EXPECT_CALL(*tlcl_, Init()).WillOnce(Return(0));
  EXPECT_CALL(*tlcl_, GetOwnership(_))
      .WillOnce(DoAll(SetArgPointee<0>(true), Return(0)));
  EXPECT_CALL(*tlcl_, Close()).WillOnce(Return(0));
  EXPECT_FALSE(startup_->NeedsClobberWithoutDevModeFile());
}

TEST_F(TPMTest, NeedsClobberTPMNotOwnedEmptyDisk) {
  EXPECT_CALL(*tlcl_, Init()).WillOnce(Return(0));
  EXPECT_CALL(*tlcl_, GetOwnership(_))
      .WillOnce(DoAll(SetArgPointee<0>(false), Return(0)));
  EXPECT_CALL(*tlcl_, Close()).WillOnce(Return(0));
  EXPECT_FALSE(startup_->NeedsClobberWithoutDevModeFile());
}

#if !USE_TPM2

TEST_F(TPMTest, NeedsClobberPreservationFile) {
  LOG(INFO) << "test getuid " << getuid();
  EXPECT_CALL(*tlcl_, Init()).WillRepeatedly(Return(0));
  EXPECT_CALL(*tlcl_, GetOwnership(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(false), Return(0)));
  EXPECT_CALL(*tlcl_, Close()).WillRepeatedly(Return(0));
  EXPECT_FALSE(startup_->IsTPMOwned());
  base::FilePath preservation_file = base_dir_.Append("preservation_request");
  ASSERT_TRUE(platform_->WriteStringToFile(preservation_file, "0"));
  EXPECT_CALL(*platform_, GetOwnership(preservation_file, _, nullptr, false))
      .WillOnce(DoAll(SetArgPointee<1>(getuid()), Return(true)));
  base::FilePath cryptohome_key_file =
      base_dir_.Append("home/.shadow/cryptohome.key");
  ASSERT_TRUE(platform_->WriteStringToFile(cryptohome_key_file, "0"));
  EXPECT_FALSE(startup_->NeedsClobberWithoutDevModeFile());
}

TEST_F(TPMTest, NeedsClobberPreservationFileWrongerUid) {
  LOG(INFO) << "test getuid " << getuid();
  EXPECT_CALL(*tlcl_, Init()).WillRepeatedly(Return(0));
  EXPECT_CALL(*tlcl_, GetOwnership(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(false), Return(0)));
  EXPECT_CALL(*tlcl_, Close()).WillRepeatedly(Return(0));
  EXPECT_FALSE(startup_->IsTPMOwned());
  base::FilePath preservation_file = base_dir_.Append("preservation_request");
  ASSERT_TRUE(platform_->WriteStringToFile(preservation_file, "0"));
  EXPECT_CALL(*platform_, GetOwnership(preservation_file, _, nullptr, false))
      .WillOnce(DoAll(SetArgPointee<1>(-1), Return(true)));
  base::FilePath cryptohome_key_file =
      base_dir_.Append("home/.shadow/cryptohome.key");
  ASSERT_TRUE(platform_->WriteStringToFile(cryptohome_key_file, "0"));
  EXPECT_TRUE(startup_->NeedsClobberWithoutDevModeFile());
}

#endif  // !USE_TPM2

TEST_F(TPMTest, NeedsClobberCryptohomeKeyFile) {
  LOG(INFO) << "test getuid " << getuid();
  EXPECT_CALL(*tlcl_, Init()).WillRepeatedly(Return(0));
  EXPECT_CALL(*tlcl_, GetOwnership(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(false), Return(0)));
  EXPECT_CALL(*tlcl_, Close()).WillRepeatedly(Return(0));
  EXPECT_FALSE(startup_->IsTPMOwned());
  base::FilePath cryptohome_key_file =
      base_dir_.Append("home/.shadow/cryptohome.key");
  ASSERT_TRUE(platform_->WriteStringToFile(cryptohome_key_file, "0"));
  EXPECT_TRUE(startup_->NeedsClobberWithoutDevModeFile());
}

TEST_F(TPMTest, NeedsClobberNeedFinalization) {
  LOG(INFO) << "test getuid " << getuid();
  EXPECT_CALL(*tlcl_, Init()).WillRepeatedly(Return(0));
  EXPECT_CALL(*tlcl_, GetOwnership(_))
      .WillRepeatedly(DoAll(SetArgPointee<0>(false), Return(0)));
  EXPECT_CALL(*tlcl_, Close()).WillRepeatedly(Return(0));
  EXPECT_FALSE(startup_->IsTPMOwned());
  base::FilePath need_finalization_file =
      base_dir_.Append("encrypted.needs-finalization");
  ASSERT_TRUE(platform_->WriteStringToFile(need_finalization_file, "0"));
  EXPECT_TRUE(startup_->NeedsClobberWithoutDevModeFile());
}

TEST_F(TPMTest, PcrExtended) {
  bool needs_extend = (!USE_TPM_INSECURE_FALLBACK) && USE_TPM2;
  if (needs_extend) {
    constexpr int kPcrNum = 13;
    constexpr uint8_t kExpectedHash[] = {41,  159, 195, 247, 59,  231, 174, 233,
                                         48,  192, 33,  135, 113, 201, 177, 10,
                                         181, 241, 127, 20,  155, 7,   115, 37,
                                         163, 95,  217, 115, 174, 118, 14,  67};
    base::FilePath cmdline_path = base_dir_.Append(kProcCmdLine);
    ASSERT_TRUE(
        platform_->WriteStringToFile(cmdline_path, "TEST_LSB_CONTENT=true"));

    EXPECT_CALL(*tlcl_, Init()).WillOnce(Return(0));
    EXPECT_CALL(*tlcl_, Extend(kPcrNum,
                               std::vector<uint8_t>(
                                   kExpectedHash,
                                   kExpectedHash + sizeof(kExpectedHash)),
                               _))
        .WillOnce(Return(0));
    EXPECT_CALL(*tlcl_, Close()).WillOnce(Return(0));
  } else {
    EXPECT_CALL(*tlcl_, Init()).Times(0);
    EXPECT_CALL(*tlcl_, Extend(_, _, _)).Times(0);
    EXPECT_CALL(*tlcl_, Close()).Times(0);
  }

  EXPECT_TRUE(startup_->ExtendPCRForVersionAttestation());
}

class StatefulWipeTest : public ::testing::Test {
 protected:
  StatefulWipeTest() {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    stateful_ = base_dir_.Append("mnt/stateful_partition");
    platform_ = std::make_unique<libstorage::FakePlatform>();
    crossystem_ = platform_->GetCrosssystem();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    std::unique_ptr<hwsec_foundation::MockTlclWrapper> tlcl =
        std::make_unique<hwsec_foundation::MockTlclWrapper>();
    tlcl_ = tlcl.get();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::make_unique<vpd::Vpd>(), flags_, base_dir_, stateful_, base_dir_,
        platform_.get(), startup_dep_.get(),
        std::make_unique<startup::StandardMountHelper>(
            platform_.get(), startup_dep_.get(), flags_, base_dir_, stateful_,
            false),
        std::move(tlcl));
    clobber_test_log_ = base_dir_.Append("clobber_test_log");
    ASSERT_TRUE(platform_->CreateDirectory(stateful_));
  }

  crossystem::Crossystem* crossystem_;
  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  base::FilePath stateful_;
  std::unique_ptr<libstorage::FakePlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  hwsec_foundation::MockTlclWrapper* tlcl_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
  base::FilePath clobber_test_log_;
};

// Tests path for requested powerwash, but the reset file is now owned by us.
TEST_F(StatefulWipeTest, PowerwashForced) {
  base::FilePath reset_file = stateful_.Append("factory_install_reset");
  startup_dep_->SetClobberLogFile(clobber_test_log_);
  ASSERT_TRUE(platform_->CreateSymbolicLink(reset_file,
                                            base::FilePath("/file_not_exist")));
  startup_->CheckForStatefulWipe();
  EXPECT_EQ(startup_dep_->GetBootAlertForArg("power_wash"), 1);
  std::string res;
  ASSERT_TRUE(platform_->ReadFileToString(clobber_test_log_, &res));
  EXPECT_EQ(res, "Powerwash initiated by Reset file presence, but invalid");
  std::set<std::string> expected = {"keepimg"};
  EXPECT_EQ(startup_dep_->GetClobberArgs(), expected);
}

// Tests normal path for user requested powerwash.
TEST_F(StatefulWipeTest, PowerwashNormal) {
  base::FilePath reset_file = stateful_.Append("factory_install_reset");
  startup_dep_->SetClobberLogFile(clobber_test_log_);
  ASSERT_TRUE(
      platform_->WriteStringToFile(reset_file, "keepimg slow test powerwash"));
  ASSERT_TRUE(platform_->SetOwnership(reset_file, getuid(), 8888, false));
  startup_->CheckForStatefulWipe();
  EXPECT_EQ(startup_dep_->GetBootAlertForArg("power_wash"), 1);
  std::string res;
  ASSERT_TRUE(platform_->ReadFileToString(clobber_test_log_, &res));
  EXPECT_EQ(res, "Powerwash initiated by Reset file presence");
  std::set<std::string> expected = {"keepimg", "slow", "test", "powerwash"};
  EXPECT_EQ(startup_dep_->GetClobberArgs(), expected);
}

// Test there is no wipe when there is no physical stateful partition.
TEST_F(StatefulWipeTest, NoStateDev) {
  startup_dep_->SetClobberLogFile(clobber_test_log_);
  startup_->SetStateDev(base::FilePath());
  startup_->CheckForStatefulWipe();
  EXPECT_EQ(startup_dep_->GetBootAlertForArg("power_wash"), 0);
  EXPECT_EQ(startup_dep_->GetBootAlertForArg("leave_dev"), 0);
  std::string res;
  ASSERT_FALSE(platform_->ReadFileToString(clobber_test_log_, &res));
  EXPECT_EQ(res, "");
  std::set<std::string> expected = {};
  EXPECT_EQ(startup_dep_->GetClobberArgs(), expected);
}

// Test transitioning to verified mode, dev_mode_allowed file is owned by us.
TEST_F(StatefulWipeTest, TransitionToVerifiedDevModeFile) {
  startup_dep_->SetClobberLogFile(clobber_test_log_);
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("devsw_boot", 0));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyString("mainfw_type", "not_rec"));
  base::FilePath dev_mode_allowed = base_dir_.Append(".developer_mode");
  ASSERT_TRUE(platform_->WriteStringToFile(dev_mode_allowed, "0"));
  ASSERT_TRUE(platform_->SetOwnership(dev_mode_allowed, getuid(), 8888, false));
  startup_->SetDevMode(false);
  startup_->SetDevModeAllowedFile(dev_mode_allowed);
  base::FilePath state_dev = base_dir_.Append("state_dev");
  startup_->SetStateDev(state_dev);
  startup_->CheckForStatefulWipe();
  EXPECT_EQ(startup_dep_->GetBootAlertForArg("leave_dev"), 1);
  std::string res;
  ASSERT_TRUE(platform_->ReadFileToString(clobber_test_log_, &res));
  EXPECT_EQ(res, "Leave developer mode, dev_mode file present");
  std::set<std::string> expected = {"fast", "keepimg"};
  EXPECT_EQ(startup_dep_->GetClobberArgs(), expected);
}

// Tranisitioning to verified mode, dev is a debug build.
// We only want to fast clobber the non-protected paths to
// preserve the testing tools.
TEST_F(StatefulWipeTest, TransitionToVerifiedDebugBuild) {
  startup_dep_->SetClobberLogFile(clobber_test_log_);
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("devsw_boot", 0));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyString("mainfw_type", "not_rec"));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("debug_build", 1));
  base::FilePath dev_mode_allowed = base_dir_.Append(".developer_mode");
  ASSERT_TRUE(platform_->WriteStringToFile(dev_mode_allowed, "0"));
  ASSERT_TRUE(platform_->SetOwnership(dev_mode_allowed, getuid(), 8888, false));
  startup_->SetDevMode(true);
  startup_->SetDevModeAllowedFile(dev_mode_allowed);
  base::FilePath state_dev = base_dir_.Append("state_dev");
  startup_->SetStateDev(state_dev);
  std::unique_ptr<startup::StandardMountHelper> mount_helper_ =
      std::make_unique<startup::StandardMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
          true);
  std::unique_ptr<StatefulMount> stateful_mount =
      std::make_unique<StatefulMount>(flags_, base_dir_, base_dir_,
                                      platform_.get(), startup_dep_.get(),
                                      mount_helper_.get());
  startup_->SetStatefulMount(std::move(stateful_mount));
  startup_->CheckForStatefulWipe();
  EXPECT_EQ(startup_dep_->GetBootAlertForArg("leave_dev"), 0);
  std::string res;
  ASSERT_FALSE(platform_->ReadFileToString(clobber_test_log_, &res));
  EXPECT_EQ(res, "");
  std::set<std::string> expected = {};
  EXPECT_EQ(startup_dep_->GetClobberArgs(), expected);
}

// Transitioning to dev mode, dev is not a debug build.
// Clobber should be called with |keepimg|, no need to erase
// the stateful.
TEST_F(StatefulWipeTest, TransitionToDevModeNoDebugBuild) {
  startup_dep_->SetClobberLogFile(clobber_test_log_);
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("devsw_boot", 1));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyString("mainfw_type", "not_rec"));
  base::FilePath dev_mode_allowed = base_dir_.Append(".developer_mode");
  ASSERT_TRUE(platform_->WriteStringToFile(dev_mode_allowed, "0"));
  ASSERT_TRUE(platform_->SetOwnership(dev_mode_allowed, -1, 8888, false));
  startup_->SetDevMode(false);
  startup_->SetDevModeAllowedFile(dev_mode_allowed);
  base::FilePath state_dev = base_dir_.Append("state_dev");
  startup_->SetStateDev(state_dev);
  startup_->CheckForStatefulWipe();
  EXPECT_EQ(startup_dep_->GetBootAlertForArg("enter_dev"), 1);
  std::string res;
  ASSERT_TRUE(platform_->ReadFileToString(clobber_test_log_, &res));
  EXPECT_EQ(res, "Enter developer mode");
  std::set<std::string> expected = {"keepimg"};
  EXPECT_EQ(startup_dep_->GetClobberArgs(), expected);
}

// Transitioning to dev mode, dev is a debug build.
// Only fast clobber the non-protected paths in debug build to preserve
// the testing tools.
TEST_F(StatefulWipeTest, TransitionToDevModeDebugBuild) {
  startup_dep_->SetClobberLogFile(clobber_test_log_);
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("devsw_boot", 1));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyString("mainfw_type", "not_rec"));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("debug_build", 1));
  base::FilePath dev_mode_allowed = base_dir_.Append(".developer_mode");
  ASSERT_TRUE(platform_->TouchFileDurable(dev_mode_allowed));
  ASSERT_TRUE(platform_->SetOwnership(dev_mode_allowed, -1, 8888, false));
  startup_->SetDevMode(true);
  startup_->SetDevModeAllowedFile(dev_mode_allowed);
  base::FilePath state_dev = base_dir_.Append("state_dev");
  startup_->SetStateDev(state_dev);
  std::unique_ptr<startup::StandardMountHelper> mount_helper_ =
      std::make_unique<startup::StandardMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
          true);
  std::unique_ptr<StatefulMount> stateful_mount =
      std::make_unique<StatefulMount>(flags_, base_dir_, base_dir_,
                                      platform_.get(), startup_dep_.get(),
                                      mount_helper_.get());
  startup_->SetStatefulMount(std::move(stateful_mount));
  startup_->CheckForStatefulWipe();
  EXPECT_EQ(startup_dep_->GetBootAlertForArg("leave_dev"), 0);
  std::string res;
  ASSERT_FALSE(platform_->ReadFileToString(clobber_test_log_, &res));
  std::set<std::string> expected = {};
  EXPECT_EQ(startup_dep_->GetClobberArgs(), expected);
  platform_->ReadFileToString(dev_mode_allowed, &res);
  EXPECT_EQ(res, "");
}

class TpmCleanupTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    platform_ = std::make_unique<libstorage::FakePlatform>();
    mock_startup_dep_ = std::make_unique<startup::MockStartupDep>();
    std::unique_ptr<hwsec_foundation::MockTlclWrapper> tlcl =
        std::make_unique<hwsec_foundation::MockTlclWrapper>();
    tlcl_ = tlcl.get();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::make_unique<vpd::Vpd>(), flags_, base_dir_, base_dir_, base_dir_,
        platform_.get(), mock_startup_dep_.get(),
        std::make_unique<startup::StandardMountHelper>(
            platform_.get(), mock_startup_dep_.get(), flags_, base_dir_,
            base_dir_, true),
        std::move(tlcl));
    flag_file_ = base_dir_.Append(kTpmFirmwareUpdateRequestFlagFile);
    tpm_cleanup_ = base_dir_.Append(kTpmFirmwareUpdateCleanup);
  }

  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  std::unique_ptr<libstorage::FakePlatform> platform_;
  std::unique_ptr<startup::MockStartupDep> mock_startup_dep_;
  hwsec_foundation::MockTlclWrapper* tlcl_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
  base::FilePath flag_file_;
  base::FilePath tpm_cleanup_;
};

TEST_F(TpmCleanupTest, TpmCleanupNoFlagFile) {
  EXPECT_CALL(*mock_startup_dep_, RunProcess(tpm_cleanup_)).Times(0);
  startup_->CleanupTpm();
}

TEST_F(TpmCleanupTest, TpmCleanupNoCmdPath) {
  platform_->WriteStringToFile(flag_file_, "exists");
  EXPECT_CALL(*mock_startup_dep_, RunProcess(tpm_cleanup_)).Times(0);
  startup_->CleanupTpm();
}

TEST_F(TpmCleanupTest, TpmCleanupSuccess) {
  platform_->WriteStringToFile(flag_file_, "exists");
  platform_->WriteStringToFile(tpm_cleanup_, "exists");
  EXPECT_CALL(*mock_startup_dep_, RunProcess(tpm_cleanup_)).Times(1);
  startup_->CleanupTpm();
}

class ConfigTest : public ::testing::Test {
 protected:
  ConfigTest() {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    lsb_file_ = base_dir_.Append(kLsbRelease);
    stateful_ = base_dir_.Append(kStatefulPartition);
    platform_ = std::make_unique<libstorage::MockPlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    crossystem_ = platform_->GetCrosssystem();
  }

  std::unique_ptr<startup::MountHelper> GenerateMountHelper() {
    startup::Flags flags;
    startup::ChromeosStartup::ParseFlags(&flags);
    startup::MountHelperFactory factory(platform_.get(), startup_dep_.get(),
                                        flags, base_dir_, stateful_, lsb_file_);
    return factory.Generate(crossystem_);
  }

  crossystem::Crossystem* crossystem_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  base::FilePath lsb_file_;
  base::FilePath stateful_;
  std::unique_ptr<libstorage::MockPlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
};

TEST_F(ConfigTest, NoDevMode) {
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("cros_debug", 0));
  ASSERT_TRUE(platform_->WriteStringToFile(
      lsb_file_, "CHROMEOS_RELEASE_TRACK=stable-channel\n"));
  std::unique_ptr<startup::MountHelper> helper = GenerateMountHelper();
  EXPECT_EQ(helper->GetMountHelperType(),
            startup::MountHelperType::kStandardMode);
}

TEST_F(ConfigTest, DevMode) {
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("cros_debug", 1));
  ASSERT_TRUE(platform_->WriteStringToFile(
      lsb_file_, "CHROMEOS_RELEASE_TRACK=stable-channel\n"));
  std::unique_ptr<startup::MountHelper> helper = GenerateMountHelper();
  EXPECT_EQ(helper->GetMountHelperType(),
            startup::MountHelperType::kStandardMode);
}

TEST_F(ConfigTest, DevModeTest) {
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("cros_debug", 1));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("debug_build", 0));
  ASSERT_TRUE(platform_->WriteStringToFile(
      lsb_file_, "CHROMEOS_RELEASE_TRACK=testimage-channel\n"));
  std::string test_lsb;
  ASSERT_TRUE(platform_->ReadFileToString(lsb_file_, &test_lsb));
  std::unique_ptr<startup::MountHelper> helper = GenerateMountHelper();
  EXPECT_EQ(helper->GetMountHelperType(), startup::MountHelperType::kTestMode);
}

TEST_F(ConfigTest, DevModeTestFactoryTest) {
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("cros_debug", 1));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("debug_build", 1));
  ASSERT_TRUE(platform_->WriteStringToFile(
      lsb_file_, "CHROMEOS_RELEASE_TRACK=testimage-channel\n"));
  base::FilePath factory_en = stateful_.Append("dev_image/factory/enabled");
  ASSERT_TRUE(platform_->WriteStringToFile(factory_en, "Enabled"));
  std::unique_ptr<startup::MountHelper> helper = GenerateMountHelper();
  EXPECT_EQ(helper->GetMountHelperType(),
            startup::MountHelperType::kFactoryMode);
}

TEST_F(ConfigTest, DevModeTestFactoryInstaller) {
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("cros_debug", 1));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("debug_build", 0));
  ASSERT_TRUE(platform_->WriteStringToFile(
      lsb_file_, "CHROMEOS_RELEASE_TRACK=testimage-channel\n"));
  base::FilePath cmdline = base_dir_.Append(kProcCmdLine);
  ASSERT_TRUE(platform_->WriteStringToFile(cmdline, "cros_factory_install"));
  std::unique_ptr<startup::MountHelper> helper = GenerateMountHelper();
  EXPECT_EQ(helper->GetMountHelperType(),
            startup::MountHelperType::kFactoryMode);
}

TEST_F(ConfigTest, DevModeTestFactoryInstallerUsingFile) {
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("cros_debug", 1));
  ASSERT_TRUE(crossystem_->VbSetSystemPropertyInt("debug_build", 0));
  ASSERT_TRUE(platform_->WriteStringToFile(
      lsb_file_, "CHROMEOS_RELEASE_TRACK=testimage-channel\n"));
  base::FilePath cmdline = base_dir_.Append(kProcCmdLine);
  ASSERT_TRUE(platform_->WriteStringToFile(cmdline, "not_factory_install"));
  base::FilePath installer = base_dir_.Append("root/.factory_installer");
  ASSERT_TRUE(platform_->WriteStringToFile(installer, "factory"));
  std::unique_ptr<startup::MountHelper> helper = GenerateMountHelper();
  EXPECT_EQ(helper->GetMountHelperType(),
            startup::MountHelperType::kFactoryMode);
}

class MountStackTest : public ::testing::Test {
 protected:
  MountStackTest()
      : base_dir_(base::FilePath("")),
        platform_(std::make_unique<libstorage::FakePlatform>()),
        startup_dep_(
            std::make_unique<startup::FakeStartupDep>(platform_.get())),
        mount_helper_(platform_.get(),
                      startup_dep_.get(),
                      flags_,
                      base_dir_,
                      base_dir_,
                      true) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
  }

  startup::Flags flags_;
  base::FilePath base_dir_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<libstorage::FakePlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  startup::StandardMountHelper mount_helper_;
};

TEST_F(MountStackTest, RememberMount) {
  std::stack<base::FilePath> mount_stack = {};
  std::deque<base::FilePath> end = {base::FilePath("/home"),
                                    base::FilePath("/root")};
  std::stack<base::FilePath> end_stack(end);
  base::FilePath mount("/home");
  mount_helper_.RememberMount(mount);
  base::FilePath mnt("/root");
  mount_helper_.RememberMount(mnt);
  std::stack<base::FilePath> res_stack = mount_helper_.GetMountStackForTest();
  EXPECT_EQ(res_stack, end_stack);
}

TEST_F(MountStackTest, CleanupMountsNoEncrypt) {
  std::stack<base::FilePath> end_stack = {};
  std::deque<base::FilePath> mount = {base::FilePath("/home"),
                                      base::FilePath("/root")};
  std::stack<base::FilePath> mount_stack(mount);

  mount_helper_.SetMountStackForTest(mount_stack);
  std::vector<base::FilePath> mounts;
  mount_helper_.CleanupMountsStack(&mounts);
  std::stack<base::FilePath> res_stack = mount_helper_.GetMountStackForTest();
  EXPECT_EQ(res_stack, end_stack);
}

TEST(MountVarAndHomeChronosEncrypted, MountEncrypted) {
  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  base::FilePath base_dir_ = temp_dir_.GetPath();

  std::unique_ptr<libstorage::FakePlatform> platform_ =
      std::make_unique<libstorage::FakePlatform>();
  std::unique_ptr<startup::FakeStartupDep> startup_dep_ =
      std::make_unique<startup::FakeStartupDep>(platform_.get());
  startup_dep_->SetMountEncOutputForArg("", "1");
  std::unique_ptr<startup::StandardMountHelper> mount_helper_ =
      std::make_unique<startup::StandardMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
          true);

  EXPECT_TRUE(mount_helper_->MountVarAndHomeChronosEncrypted());
}

TEST(MountVarAndHomeChronosEncrypted, MountEncryptedFail) {
  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  base::FilePath base_dir_ = temp_dir_.GetPath();

  std::unique_ptr<libstorage::FakePlatform> platform_ =
      std::make_unique<libstorage::FakePlatform>();
  std::unique_ptr<startup::FakeStartupDep> startup_dep_ =
      std::make_unique<startup::FakeStartupDep>(platform_.get());
  std::unique_ptr<startup::StandardMountHelper> mount_helper_ =
      std::make_unique<startup::StandardMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
          true);

  EXPECT_FALSE(mount_helper_->MountVarAndHomeChronosEncrypted());
}

class DoMountTest : public ::testing::Test {
 protected:
  DoMountTest() {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    platform_ = std::make_unique<libstorage::MockPlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
  }

  startup::Flags flags_;
  base::FilePath base_dir_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<libstorage::MockPlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
};

TEST_F(DoMountTest, StandardMountHelper) {
  flags_.encstateful = true;
  startup_dep_->SetMountEncOutputForArg("", "1");
  std::unique_ptr<startup::StandardMountHelper> mount_helper_ =
      std::make_unique<startup::StandardMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
          true);
  EXPECT_TRUE(mount_helper_->DoMountVarAndHomeChronos());
}

TEST_F(DoMountTest, TestModeMountHelperCreateSystemKey) {
  flags_.sys_key_util = true;
  flags_.encstateful = true;
  base::FilePath no_early = base_dir_.Append(".no_early_system_key");
  base::FilePath log_file = base_dir_.Append(kSysKeyLog);
  ASSERT_TRUE(platform_->WriteStringToFile(no_early, "1"));
  ASSERT_TRUE(platform_->WriteStringToFile(log_file, "1"));

  startup_dep_->SetMountEncOutputForArg("", "1");
  std::unique_ptr<startup::TestModeMountHelper> mount_helper_ =
      std::make_unique<startup::TestModeMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
          true);
  EXPECT_TRUE(mount_helper_->DoMountVarAndHomeChronos());
  std::string sys_key_log_out;
  platform_->ReadFileToString(log_file, &sys_key_log_out);
  EXPECT_EQ(sys_key_log_out, "Opt not to create a system key in advance.");
}

TEST_F(DoMountTest, TestModeMountHelperMountEncryptFailed) {
  flags_.sys_key_util = false;
  flags_.encstateful = true;
  base::FilePath mnt_encrypt_fail = base_dir_.Append("mount_encrypted_failed");

  ASSERT_TRUE(platform_->TouchFileDurable(mnt_encrypt_fail));
  startup_dep_->SetMountEncOutputForArg("", "1");
  std::unique_ptr<startup::TestModeMountHelper> mount_helper_ =
      std::make_unique<startup::TestModeMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
          true);
  EXPECT_TRUE(mount_helper_->DoMountVarAndHomeChronos());
}

TEST_F(DoMountTest, TestModeMountHelperMountVarSuccess) {
  flags_.sys_key_util = false;
  flags_.encstateful = true;
  base::FilePath clobber_log_ = base_dir_.Append("clobber_test_log");
  startup_dep_->SetClobberLogFile(clobber_log_);

  startup_dep_->SetMountEncOutputForArg("", "1");
  std::unique_ptr<startup::TestModeMountHelper> mount_helper_ =
      std::make_unique<startup::TestModeMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
          true);
  EXPECT_TRUE(mount_helper_->DoMountVarAndHomeChronos());
  std::string clobber_log_out;
  platform_->ReadFileToString(clobber_log_, &clobber_log_out);
  EXPECT_EQ(clobber_log_out, "");
}

TEST_F(DoMountTest, TestModeMountHelperMountVarFail) {
  flags_.sys_key_util = false;
  flags_.encstateful = true;
  base::FilePath clobber_log_ = base_dir_.Append("clobber_test_log");
  startup_dep_->SetClobberLogFile(clobber_log_);
  base::FilePath mnt_encrypt_fail = base_dir_.Append("mount_encrypted_failed");
  EXPECT_CALL(*platform_, GetOwnership(mnt_encrypt_fail, _, nullptr, false))
      .WillOnce(DoAll(SetArgPointee<1>(getuid()), Return(true)));
  base::FilePath corrupted_enc = base_dir_.Append("corrupted_encryption");
  base::FilePath encrypted_test = base_dir_.Append("encrypted.test1");
  base::FilePath encrypted_test2 = base_dir_.Append("encrypted.test2");
  ASSERT_TRUE(platform_->WriteStringToFile(encrypted_test, "1"));
  ASSERT_TRUE(platform_->WriteStringToFile(encrypted_test2, "1"));

  std::unique_ptr<startup::TestModeMountHelper> mount_helper_ =
      std::make_unique<startup::TestModeMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
          true);
  EXPECT_FALSE(mount_helper_->DoMountVarAndHomeChronos());
  std::string clobber_log_out;
  platform_->ReadFileToString(clobber_log_, &clobber_log_out);
  EXPECT_EQ(clobber_log_out,
            "Failed mounting var and home/chronos; re-created.");
  EXPECT_TRUE(platform_->FileExists(corrupted_enc.Append("encrypted.test1")));
  EXPECT_TRUE(platform_->FileExists(corrupted_enc.Append("encrypted.test2")));
}

TEST_F(DoMountTest, FactoryModeMountHelperTmpfsFailMntVar) {
  flags_.encstateful = true;
  base::FilePath options_file = base_dir_.Append(kMntOptionsFile);
  ASSERT_TRUE(platform_->WriteStringToFile(options_file, "tmpfs"));
  base::FilePath var = base_dir_.Append("var");
  EXPECT_CALL(*platform_, Mount(_, var, "tmpfs", _, _)).WillOnce(Return(false));

  std::unique_ptr<startup::FactoryModeMountHelper> mount_helper_ =
      std::make_unique<startup::FactoryModeMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
          true);
  EXPECT_FALSE(mount_helper_->DoMountVarAndHomeChronos());
}

TEST_F(DoMountTest, FactoryModeMountHelperTmpfsFailMntHomeChronos) {
  flags_.encstateful = true;
  base::FilePath options_file = base_dir_.Append(kMntOptionsFile);
  ASSERT_TRUE(platform_->WriteStringToFile(options_file, "tmpfs"));
  base::FilePath var = base_dir_.Append("var");
  EXPECT_CALL(*platform_, Mount(_, var, "tmpfs", _, _)).WillOnce(Return(true));

  base::FilePath stateful_home_chronos = base_dir_.Append("home/chronos");
  EXPECT_CALL(*platform_, Mount(stateful_home_chronos, _, _, _, _))
      .WillOnce(Return(false));

  std::unique_ptr<startup::FactoryModeMountHelper> mount_helper_ =
      std::make_unique<startup::FactoryModeMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
          true);
  EXPECT_FALSE(mount_helper_->DoMountVarAndHomeChronos());
}

TEST_F(DoMountTest, FactoryModeMountHelperTmpfsSuccess) {
  flags_.encstateful = true;
  base::FilePath stateful = base_dir_.Append("mnt/stateful_partition");
  base::FilePath options_file = stateful.Append(kMntOptionsFile);
  ASSERT_TRUE(platform_->WriteStringToFile(options_file, "tmpfs"));
  base::FilePath var = base_dir_.Append("var");
  EXPECT_CALL(*platform_, Mount(_, var, "tmpfs", _, _)).WillOnce(Return(true));

  base::FilePath stateful_home_chronos = stateful.Append("home/chronos");
  base::FilePath home_chronos = base_dir_.Append("home/chronos");
  EXPECT_CALL(*platform_,
              Mount(stateful_home_chronos, home_chronos, _, MS_BIND, _))
      .WillOnce(Return(true));

  std::unique_ptr<startup::FactoryModeMountHelper> mount_helper_ =
      std::make_unique<startup::FactoryModeMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, stateful,
          true);
  EXPECT_TRUE(mount_helper_->DoMountVarAndHomeChronos());
}

TEST_F(DoMountTest, FactoryModeMountHelperUnencryptFailMntVar) {
  flags_.encstateful = true;
  base::FilePath stateful = base_dir_.Append("mnt/stateful_partition");
  base::FilePath stateful_var = stateful.Append("var");
  EXPECT_CALL(*platform_, Mount(stateful_var, _, _, MS_BIND, _))
      .WillOnce(Return(false));

  std::unique_ptr<startup::FactoryModeMountHelper> mount_helper_ =
      std::make_unique<startup::FactoryModeMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, stateful,
          true);
  EXPECT_FALSE(mount_helper_->DoMountVarAndHomeChronos());
}

TEST_F(DoMountTest, FactoryModeMountHelperUnencryptFailMntHomeChronos) {
  flags_.encstateful = true;
  base::FilePath stateful = base_dir_.Append("mnt/stateful_partition");
  base::FilePath stateful_var = stateful.Append("var");
  base::FilePath var = base_dir_.Append("var");
  EXPECT_CALL(*platform_, Mount(stateful_var, var, _, MS_BIND, _))
      .WillOnce(Return(true));

  base::FilePath stateful_home_chronos = stateful.Append("home/chronos");
  EXPECT_CALL(*platform_, Mount(stateful_home_chronos, _, _, MS_BIND, _))
      .WillOnce(Return(false));

  std::unique_ptr<startup::FactoryModeMountHelper> mount_helper_ =
      std::make_unique<startup::FactoryModeMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, stateful,
          true);
  EXPECT_FALSE(mount_helper_->DoMountVarAndHomeChronos());
}

TEST_F(DoMountTest, FactoryModeMountHelperUnencryptSuccess) {
  flags_.encstateful = true;
  base::FilePath stateful = base_dir_.Append("mnt/stateful_partition");
  base::FilePath stateful_var = stateful.Append("var");
  base::FilePath var = base_dir_.Append("var");
  EXPECT_CALL(*platform_, Mount(stateful_var, var, _, MS_BIND, _))
      .WillOnce(Return(true));

  base::FilePath stateful_home_chronos = stateful.Append("home/chronos");
  base::FilePath home_chronos = base_dir_.Append("home/chronos");
  EXPECT_CALL(*platform_,
              Mount(stateful_home_chronos, home_chronos, _, MS_BIND, _))
      .WillOnce(Return(true));

  std::unique_ptr<startup::FactoryModeMountHelper> mount_helper_ =
      std::make_unique<startup::FactoryModeMountHelper>(
          platform_.get(), startup_dep_.get(), flags_, base_dir_, stateful,
          true);
  EXPECT_TRUE(mount_helper_->DoMountVarAndHomeChronos());
}

class IsVarFullTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    platform_ = std::make_unique<libstorage::MockPlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    std::unique_ptr<hwsec_foundation::MockTlclWrapper> tlcl =
        std::make_unique<hwsec_foundation::MockTlclWrapper>();
    tlcl_ = tlcl.get();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::make_unique<vpd::Vpd>(), flags_, base_dir_, base_dir_, base_dir_,
        platform_.get(), startup_dep_.get(),
        std::make_unique<startup::StandardMountHelper>(
            platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
            false),
        std::move(tlcl));
  }

  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  std::unique_ptr<libstorage::MockPlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  hwsec_foundation::MockTlclWrapper* tlcl_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
};

TEST_F(IsVarFullTest, StatvfsFailure) {
  // Assume the machine storage where the unit test are running is not full.
  EXPECT_FALSE(startup_->IsVarFull());
}

TEST_F(IsVarFullTest, Failure) {
  base::FilePath var = base_dir_.Append("var");

  struct statvfs st;
  st.f_bavail = 2600;
  st.f_favail = 110;
  st.f_bsize = 4096;
  EXPECT_CALL(*platform_, StatVFS(var, _))
      .WillOnce(DoAll(SetArgPointee<1>(st), Return(true)));
  EXPECT_FALSE(startup_->IsVarFull());
}

TEST_F(IsVarFullTest, TrueBavail) {
  base::FilePath var = base_dir_.Append("var");

  struct statvfs st;
  st.f_bavail = 1000;
  st.f_favail = 110;
  st.f_bsize = 4096;
  EXPECT_CALL(*platform_, StatVFS(var, _))
      .WillOnce(DoAll(SetArgPointee<1>(st), Return(true)));
  EXPECT_TRUE(startup_->IsVarFull());
}

TEST_F(IsVarFullTest, TrueFavail) {
  base::FilePath var = base_dir_.Append("var");

  struct statvfs st;
  st.f_bavail = 11000;
  st.f_favail = 90;
  st.f_bsize = 4096;
  EXPECT_CALL(*platform_, StatVFS(var, _))
      .WillOnce(DoAll(SetArgPointee<1>(st), Return(true)));
  EXPECT_TRUE(startup_->IsVarFull());
}

class DeviceSettingsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    platform_ = std::make_unique<libstorage::FakePlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    std::unique_ptr<hwsec_foundation::MockTlclWrapper> tlcl =
        std::make_unique<hwsec_foundation::MockTlclWrapper>();
    tlcl_ = tlcl.get();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::make_unique<vpd::Vpd>(), flags_, base_dir_, base_dir_, base_dir_,
        platform_.get(), startup_dep_.get(),
        std::make_unique<startup::StandardMountHelper>(
            platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
            false),
        std::move(tlcl));
    base::FilePath var_lib = base_dir_.Append("var/lib");
    whitelist_ = var_lib.Append("whitelist");
    devicesettings_ = var_lib.Append("devicesettings");
  }

  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  std::unique_ptr<libstorage::FakePlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  hwsec_foundation::MockTlclWrapper* tlcl_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
  base::FilePath whitelist_;
  base::FilePath devicesettings_;
};

TEST_F(DeviceSettingsTest, OldPathEmpty) {
  ASSERT_TRUE(platform_->CreateDirectory(whitelist_));
  base::FilePath devicesettings_test = devicesettings_.Append("test");
  ASSERT_TRUE(platform_->WriteStringToFile(devicesettings_test, "test"));

  startup_->MoveToLibDeviceSettings();
  EXPECT_FALSE(platform_->DirectoryExists(whitelist_));
  EXPECT_TRUE(platform_->FileExists(devicesettings_test));
}

TEST_F(DeviceSettingsTest, NewPathEmpty) {
  ASSERT_TRUE(platform_->CreateDirectory(whitelist_));
  ASSERT_TRUE(platform_->CreateDirectory(devicesettings_));
  base::FilePath whitelist_test = whitelist_.Append("test");
  ASSERT_TRUE(platform_->WriteStringToFile(whitelist_test, "test"));
  base::FilePath devicesettings_test = devicesettings_.Append("test");

  startup_->MoveToLibDeviceSettings();
  EXPECT_FALSE(platform_->DirectoryExists(whitelist_));
  EXPECT_FALSE(platform_->FileExists(whitelist_test));
  EXPECT_TRUE(platform_->FileExists(devicesettings_test));
}

TEST_F(DeviceSettingsTest, NeitherPathEmpty) {
  ASSERT_TRUE(platform_->CreateDirectory(whitelist_));
  ASSERT_TRUE(platform_->CreateDirectory(devicesettings_));
  base::FilePath whitelist_test = whitelist_.Append("test_w");
  ASSERT_TRUE(platform_->WriteStringToFile(whitelist_test, "test_w"));
  base::FilePath devicesettings_test = devicesettings_.Append("test_d");
  ASSERT_TRUE(platform_->WriteStringToFile(devicesettings_test, "test_d"));

  startup_->MoveToLibDeviceSettings();
  EXPECT_TRUE(platform_->DirectoryExists(whitelist_));
  EXPECT_TRUE(platform_->FileExists(whitelist_test));
  EXPECT_TRUE(platform_->FileExists(devicesettings_test));
}

class DaemonStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    platform_ = std::make_unique<libstorage::MockPlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    std::unique_ptr<hwsec_foundation::MockTlclWrapper> tlcl =
        std::make_unique<hwsec_foundation::MockTlclWrapper>();
    tlcl_ = tlcl.get();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::make_unique<vpd::Vpd>(), flags_, base_dir_, base_dir_, base_dir_,
        platform_.get(), startup_dep_.get(),
        std::make_unique<startup::StandardMountHelper>(
            platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
            true),
        std::move(tlcl));
  }

  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  std::unique_ptr<libstorage::MockPlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  hwsec_foundation::MockTlclWrapper* tlcl_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
};

TEST_F(DaemonStoreTest, NonEmptyEtc) {
  base::FilePath run = base_dir_.Append("run");
  base::FilePath etc = base_dir_.Append("etc");
  base::FilePath run_daemon = run.Append("daemon-store");
  base::FilePath run_daemon_cache = run.Append("daemon-store-cache");
  base::FilePath etc_daemon = etc.Append("daemon-store");
  base::FilePath etc_file = etc_daemon.Append("test_file");
  base::FilePath etc_file_not_ds = etc.Append("test/not_incl");
  ASSERT_TRUE(platform_->WriteStringToFile(etc_file, "1"));
  ASSERT_TRUE(platform_->WriteStringToFile(etc_file_not_ds, "exclude"));
  base::FilePath subdir = etc_daemon.Append("subdir");
  base::FilePath sub_file = subdir.Append("test_file");
  ASSERT_TRUE(platform_->WriteStringToFile(sub_file, "1"));

  base::FilePath run_subdir = run_daemon.Append("subdir");
  base::FilePath run_cache_subdir = run_daemon_cache.Append("subdir");
  base::FilePath run_test_exclude = run.Append("test/not_incl");
  base::FilePath run_ds_exclude = run_daemon.Append("test/not_incl");
  EXPECT_CALL(*platform_, Mount(run_subdir, run_subdir, _, MS_BIND, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, Mount(base::FilePath(), run_subdir, _, MS_SHARED, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              Mount(run_cache_subdir, run_cache_subdir, _, MS_BIND, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_,
              Mount(base::FilePath(), run_cache_subdir, _, MS_SHARED, _))
      .WillOnce(Return(true));

  startup_->CreateDaemonStore();
  EXPECT_TRUE(platform_->DirectoryExists(run_subdir));
  EXPECT_TRUE(platform_->DirectoryExists(run_cache_subdir));
  EXPECT_FALSE(platform_->FileExists(run_test_exclude));
  EXPECT_FALSE(platform_->FileExists(run_ds_exclude));
}

class RemoveVarEmptyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    platform_ = std::make_unique<libstorage::FakePlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    base_dir_ = temp_dir_.GetPath();
    std::unique_ptr<hwsec_foundation::MockTlclWrapper> tlcl =
        std::make_unique<hwsec_foundation::MockTlclWrapper>();
    tlcl_ = tlcl.get();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::make_unique<vpd::Vpd>(), flags_, base_dir_, base_dir_, base_dir_,
        platform_.get(), startup_dep_.get(),
        std::make_unique<startup::StandardMountHelper>(
            platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
            true),
        std::move(tlcl));
  }

  std::unique_ptr<libstorage::FakePlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  hwsec_foundation::MockTlclWrapper* tlcl_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
};

TEST_F(RemoveVarEmptyTest, NonEmpty) {
  base::FilePath var_empty = base_dir_.Append("var/empty");
  base::FilePath file1 = var_empty.Append("test_file");
  ASSERT_TRUE(platform_->WriteStringToFile(file1, "1"));
  base::FilePath file2 = var_empty.Append("test_file_2");
  ASSERT_TRUE(platform_->WriteStringToFile(file2, "1"));

  startup_->RemoveVarEmpty();
  EXPECT_TRUE(!platform_->FileExists(file1));
  EXPECT_TRUE(!platform_->FileExists(file2));
  EXPECT_TRUE(!platform_->FileExists(var_empty));
}

class CheckVarLogTest : public ::testing::Test {
 protected:
  CheckVarLogTest() {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    platform_ = std::make_unique<libstorage::FakePlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    base_dir_ = temp_dir_.GetPath();
    std::unique_ptr<hwsec_foundation::MockTlclWrapper> tlcl =
        std::make_unique<hwsec_foundation::MockTlclWrapper>();
    tlcl_ = tlcl.get();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::make_unique<vpd::Vpd>(), flags_, base_dir_, base_dir_, base_dir_,
        platform_.get(), startup_dep_.get(),
        std::make_unique<startup::StandardMountHelper>(
            platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
            true),
        std::move(tlcl));
    var_log_ = base_dir_.Append("var/log");
    platform_->CreateDirectory(var_log_);
  }

  std::unique_ptr<libstorage::FakePlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  hwsec_foundation::MockTlclWrapper* tlcl_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
  base::FilePath var_log_;
};

TEST_F(CheckVarLogTest, NoSymLinks) {
  base::FilePath test_file = var_log_.Append("test_file");
  base::FilePath test_dir = var_log_.Append("test_dir");
  base::FilePath test_test = test_dir.Append("test");
  ASSERT_TRUE(platform_->WriteStringToFile(test_file, "test1"));
  ASSERT_TRUE(platform_->WriteStringToFile(test_test, "test2"));

  startup_->CheckVarLog();
  EXPECT_TRUE(platform_->FileExists(test_file));
  EXPECT_TRUE(platform_->FileExists(test_test));
}

TEST_F(CheckVarLogTest, SymLinkInsideVarLog) {
  base::FilePath test_file = var_log_.Append("test_file");
  base::FilePath test_dir = var_log_.Append("test_dir");
  base::FilePath test_test = test_dir.Append("test");
  base::FilePath test_link = var_log_.Append("test_link");
  base::FilePath test_sub_link = test_dir.Append("link");
  ASSERT_TRUE(platform_->WriteStringToFile(test_file, "test1"));
  ASSERT_TRUE(platform_->WriteStringToFile(test_test, "test2"));
  ASSERT_TRUE(platform_->CreateSymbolicLink(test_link, test_file));
  ASSERT_TRUE(platform_->CreateSymbolicLink(test_sub_link, test_test));

  startup_->CheckVarLog();
  EXPECT_TRUE(platform_->FileExists(test_file));
  EXPECT_TRUE(platform_->FileExists(test_test));
  EXPECT_TRUE(platform_->IsLink(test_link));
  EXPECT_TRUE(platform_->IsLink(test_sub_link));
}

TEST_F(CheckVarLogTest, SymLinkOutsideVarLog) {
  base::FilePath test_file = var_log_.Append("test_file");
  base::FilePath test_dir = var_log_.Append("test_dir");
  base::FilePath test_test = test_dir.Append("test");
  base::FilePath test_link = var_log_.Append("test_link");
  base::FilePath test_sub_link = test_dir.Append("link");
  base::FilePath outside = base_dir_.Append("outside");
  ASSERT_TRUE(platform_->WriteStringToFile(outside, "out"));
  ASSERT_TRUE(platform_->WriteStringToFile(test_file, "test1"));
  ASSERT_TRUE(platform_->WriteStringToFile(test_test, "test2"));
  ASSERT_TRUE(platform_->CreateSymbolicLink(test_link, outside));
  ASSERT_TRUE(platform_->CreateSymbolicLink(test_sub_link, outside));

  startup_->CheckVarLog();
  EXPECT_TRUE(platform_->FileExists(test_file));
  EXPECT_TRUE(platform_->FileExists(test_test));
  EXPECT_FALSE(platform_->FileExists(test_link));
  EXPECT_FALSE(platform_->FileExists(test_sub_link));
}

class DevMountPackagesTest : public ::testing::Test {
 protected:
  DevMountPackagesTest() {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    stateful_ = base_dir_.Append("stateful_test");
    base::CreateDirectory(stateful_);
    platform_ = std::make_unique<libstorage::MockPlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    mount_helper_ = std::make_unique<startup::StandardMountHelper>(
        platform_.get(), startup_dep_.get(), flags_, base_dir_, stateful_,
        true);
    stateful_mount_ = std::make_unique<startup::StatefulMount>(
        flags_, base_dir_, stateful_, platform_.get(), startup_dep_.get(),
        mount_helper_.get());
    proc_mounts_ = base_dir_.Append("proc/mounts");
    mount_log_ = base_dir_.Append("var/log/mount_options.log");
    stateful_dev_image_ = stateful_.Append("dev_image");
    usrlocal_ = base_dir_.Append("usr/local");
    asan_dir_ = base_dir_.Append("var/log/asan");
    lsm_dir_ = base_dir_.Append(kLSMDir);
    allow_sym_ = lsm_dir_.Append("allow_symlink");
    disable_ssh_ = base_dir_.Append(
        "usr/share/cros/startup/disable_stateful_security_hardening");
    var_overlay_ = stateful_.Append("var_overlay");
    var_portage_ = base_dir_.Append("var/lib/portage");
    ASSERT_TRUE(platform_->WriteStringToFile(allow_sym_, ""));
    ASSERT_TRUE(platform_->CreateDirectory(stateful_dev_image_));
    ASSERT_TRUE(platform_->CreateDirectory(usrlocal_));
    ASSERT_TRUE(platform_->CreateDirectory(var_overlay_));
    ASSERT_TRUE(platform_->CreateDirectory(var_portage_));

    // Collect all writes to sysfs attribute "allow_symlink" to a local
    // variable, ignore the rest.
    EXPECT_CALL(*platform_, WriteStringToFile(_, _)).Times(AnyNumber());
    EXPECT_CALL(*platform_, WriteStringToFile(allow_sym_, _))
        .WillRepeatedly(
            [this](const base::FilePath&, const std::string& c) -> bool {
              allow_sym_contents_ += c;
              return true;
            });
  }

  startup::Flags flags_;
  base::FilePath base_dir_;
  std::unique_ptr<libstorage::MockPlatform> platform_;
  base::FilePath stateful_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  std::unique_ptr<startup::StandardMountHelper> mount_helper_;
  std::unique_ptr<startup::StatefulMount> stateful_mount_;
  base::ScopedTempDir temp_dir_;
  base::FilePath proc_mounts_;
  base::FilePath mount_log_;
  base::FilePath stateful_dev_image_;
  base::FilePath usrlocal_;
  base::FilePath asan_dir_;
  base::FilePath lsm_dir_;
  base::FilePath allow_sym_;
  std::string allow_sym_contents_;
  base::FilePath disable_ssh_;
  base::FilePath var_overlay_;
  base::FilePath var_portage_;
};

TEST_F(DevMountPackagesTest, NoDeviceDisableStatefulSecurity) {
  platform_->CreateDirectory(disable_ssh_);

  std::string mount_contents =
      R"(/dev/root / ext2 ro,seclabel,relatime 0 0 )"
      R"(devtmpfs /dev devtmpfs rw,seclabel,nosuid,noexec,relatime,)"
      R"(size=4010836k,nr_inodes=1002709,mode=755 0 0)"
      R"(proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0)"
      R"(sysfs /sys sysfs rw,seclabel,nosuid,nodev,noexec,relatime 0 0)";

  ASSERT_TRUE(platform_->WriteStringToFile(proc_mounts_, mount_contents));

  EXPECT_CALL(*platform_, Mount(stateful_dev_image_, usrlocal_, _, MS_BIND, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, Mount(base::FilePath(), usrlocal_, _, MS_REMOUNT, _))
      .WillOnce(Return(true));

  stateful_mount_->DevMountPackages(base::FilePath());

  EXPECT_TRUE(platform_->DirectoryExists(asan_dir_));

  std::string mount_log_contents;
  platform_->ReadFileToString(mount_log_, &mount_log_contents);
  EXPECT_EQ(mount_contents, mount_log_contents);

  EXPECT_EQ("", allow_sym_contents_);
}

TEST_F(DevMountPackagesTest, WithDeviceNoDisableStatefulSecurity) {
  std::string mount_contents =
      R"(/dev/root / ext2 ro,seclabel,relatime 0 0 )"
      R"(devtmpfs /dev devtmpfs rw,seclabel,nosuid,noexec,relatime,)"
      R"(size=4010836k,nr_inodes=1002709,mode=755 0 0)"
      R"(proc /proc proc rw,nosuid,nodev,noexec,relatime 0 0)"
      R"(sysfs /sys sysfs rw,seclabel,nosuid,nodev,noexec,relatime 0 0)";

  ASSERT_TRUE(platform_->WriteStringToFile(proc_mounts_, mount_contents));

  EXPECT_CALL(*platform_, Mount(stateful_dev_image_, usrlocal_, _, MS_BIND, _))
      .WillOnce(Return(true));
  EXPECT_CALL(*platform_, Mount(base::FilePath(), usrlocal_, _, MS_REMOUNT, _))
      .WillOnce(Return(true));

  base::FilePath portage = var_overlay_.Append("lib/portage");
  ASSERT_TRUE(platform_->CreateDirectory(portage));
  EXPECT_CALL(*platform_, Mount(portage, var_portage_, _, MS_BIND, _))
      .WillOnce(Return(true));

  stateful_mount_->DevMountPackages(base::FilePath());

  EXPECT_TRUE(platform_->DirectoryExists(asan_dir_));

  std::string mount_log_contents;
  platform_->ReadFileToString(mount_log_, &mount_log_contents);
  EXPECT_EQ(mount_contents, mount_log_contents);

  // 2 locations are allowed to have symlinks: portage and dev_image.
  EXPECT_EQ(
      base_dir_.Append("var/tmp/portage").value() + stateful_dev_image_.value(),
      allow_sym_contents_);
}

class RestoreContextsForVarTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    platform_ = std::make_unique<libstorage::FakePlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    std::unique_ptr<hwsec_foundation::MockTlclWrapper> tlcl =
        std::make_unique<hwsec_foundation::MockTlclWrapper>();
    tlcl_ = tlcl.get();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::make_unique<vpd::Vpd>(), flags_, base_dir_, base_dir_, base_dir_,
        platform_.get(), startup_dep_.get(),
        std::make_unique<startup::StandardMountHelper>(
            platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
            true),
        std::move(tlcl));
  }

  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  hwsec_foundation::MockTlclWrapper* tlcl_;

  std::unique_ptr<libstorage::FakePlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
};

TEST_F(RestoreContextsForVarTest, Restorecon) {
  base::FilePath var = base_dir_.Append("var");
  ASSERT_TRUE(platform_->CreateDirectory(var));
  base::FilePath debug = base_dir_.Append("sys/kernel/debug");
  ASSERT_TRUE(platform_->CreateDirectory(debug));
  base::FilePath shadow = base_dir_.Append("home/.shadow");
  ASSERT_TRUE(platform_->CreateDirectory(shadow));

  base::FilePath selinux = base_dir_.Append("sys/fs/selinux/enforce");
  ASSERT_TRUE(platform_->WriteStringToFile(selinux, "1"));

  startup_->RestoreContextsForVar(&RestoreconTestFunc);

  EXPECT_TRUE(platform_->FileExists(var.Append("restore")));
  EXPECT_TRUE(platform_->FileExists(shadow.Append("restore")));
  EXPECT_TRUE(platform_->FileExists(debug.Append("exclude")));
}

class RestorePreservedPathsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    platform_ = std::make_unique<libstorage::FakePlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
    base_dir_ = temp_dir_.GetPath();
    stateful_ = base_dir_.Append("stateful_test");
    platform_->CreateDirectory(stateful_);
    std::unique_ptr<hwsec_foundation::MockTlclWrapper> tlcl =
        std::make_unique<hwsec_foundation::MockTlclWrapper>();
    tlcl_ = tlcl.get();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::make_unique<vpd::Vpd>(), flags_, base_dir_, stateful_, base_dir_,
        platform_.get(), startup_dep_.get(),
        std::make_unique<startup::StandardMountHelper>(
            platform_.get(), startup_dep_.get(), flags_, base_dir_, base_dir_,
            true),
        std::move(tlcl));
    startup_->SetDevMode(true);
  }

  std::unique_ptr<libstorage::FakePlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  base::FilePath stateful_;
  hwsec_foundation::MockTlclWrapper* tlcl_;

  std::unique_ptr<startup::ChromeosStartup> startup_;
};

TEST_F(RestorePreservedPathsTest, PopPaths) {
  std::string libservo("var/lib/servod");
  std::string wifi_cred("usr/local/etc/wifi_creds");
  base::FilePath preserve_dir = stateful_.Append("unencrypted/preserve");
  base::FilePath libservo_path = base_dir_.Append(libservo);
  base::FilePath wifi_cred_path = base_dir_.Append(wifi_cred);
  base::FilePath libservo_preserve = preserve_dir.Append(libservo);
  base::FilePath wifi_cred_preserve = preserve_dir.Append(wifi_cred);

  ASSERT_TRUE(
      platform_->WriteStringToFile(libservo_preserve.Append("file1"), "1"));
  ASSERT_TRUE(
      platform_->WriteStringToFile(wifi_cred_preserve.Append("file2"), "1"));

  startup_->RestorePreservedPaths();
  EXPECT_TRUE(platform_->FileExists(libservo_path.Append("file1")));
  EXPECT_TRUE(platform_->FileExists(wifi_cred_path.Append("file2")));
  EXPECT_FALSE(platform_->FileExists(libservo_preserve.Append("file1")));
  EXPECT_FALSE(platform_->FileExists(wifi_cred_preserve.Append("file2")));
}

}  // namespace startup
