// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

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
#include <gtest/gtest.h>

#include "init/crossystem.h"
#include "init/crossystem_fake.h"
#include "init/startup/chromeos_startup.h"
#include "init/startup/fake_platform_impl.h"
#include "init/startup/mock_platform_impl.h"
#include "init/startup/mount_helper.h"
#include "init/startup/platform_impl.h"

using testing::_;
using testing::AnyNumber;
using testing::ByMove;
using testing::Return;

namespace startup {

namespace {

constexpr char kTpmFirmwareUpdateCleanup[] =
    "usr/sbin/tpm-firmware-update-cleanup";
constexpr char kTpmFirmwareUpdateRequestFlagFile[] =
    "unencrypted/preserve/tpm_firmware_update_request";

// Helper function to create directory and write to file.
bool CreateDirAndWriteFile(const base::FilePath& path,
                           const std::string& contents) {
  return base::CreateDirectory(path.DirName()) &&
         base::WriteFile(path, contents.c_str(), contents.length()) ==
             contents.length();
}

}  // namespace

class EarlySetupTest : public ::testing::Test {
 protected:
  EarlySetupTest() : cros_system_(new CrosSystemFake()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    kernel_debug_ = base_dir_.Append("sys/kernel/debug");
    kernel_config_ = base_dir_.Append("sys/kernel/config");
    kernel_tracing_ = base_dir_.Append("sys/kernel/tracing");
    tracing_ = kernel_tracing_.Append("tracing_on");
    security_hardening_ = base_dir_.Append(
        "usr/share/cros/startup/disable_stateful_security_hardening");
    kernel_security_ = base_dir_.Append("sys/kernel/security");
    namespaces_ = base_dir_.Append("run/namespaces");
    platform_ = new startup::FakePlatform();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::unique_ptr<CrosSystem>(cros_system_), flags_, base_dir_, base_dir_,
        base_dir_, base_dir_, std::unique_ptr<startup::FakePlatform>(platform_),
        std::make_unique<startup::MountHelper>(
            std::make_unique<startup::FakePlatform>(), flags_, base_dir_,
            base_dir_));
  }

  CrosSystemFake* cros_system_;
  startup::Flags flags_;
  startup::FakePlatform* platform_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  base::FilePath kernel_debug_;
  base::FilePath kernel_tracing_;
  base::FilePath dev_pts_;
  base::FilePath dev_shm_;
  base::FilePath kernel_config_;
  base::FilePath tracing_;
  base::FilePath security_hardening_;
  base::FilePath kernel_security_;
  base::FilePath namespaces_;
};

TEST_F(EarlySetupTest, NoTracing) {
  platform_->SetMountResultForPath(kernel_debug_, "debugfs");
  platform_->SetMountResultForPath(kernel_tracing_, "tracefs");
  platform_->SetMountResultForPath(kernel_config_, "configfs");
  platform_->SetMountResultForPath(kernel_security_, "securityfs");
  platform_->SetMountResultForPath(namespaces_, "");

  startup_->EarlySetup();
}

class DevCheckBlockTest : public ::testing::Test {
 protected:
  DevCheckBlockTest() : cros_system_(new CrosSystemFake()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir = temp_dir_.GetPath();
    vpd_dir = base_dir.Append("sys/firmware/vpd/rw");
    vpd_file = vpd_dir.Append("block_devmode");
    dev_mode_file = base_dir.Append(".developer_mode");
    platform_ = new startup::FakePlatform();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::unique_ptr<CrosSystem>(cros_system_), flags_, base_dir, base_dir,
        base_dir, base_dir, std::unique_ptr<startup::FakePlatform>(platform_),
        std::make_unique<startup::MountHelper>(
            std::make_unique<startup::FakePlatform>(), flags_, base_dir,
            base_dir));
    ASSERT_TRUE(cros_system_->SetInt("cros_debug", 1));
    base::CreateDirectory(dev_mode_file.DirName());
    startup_->SetDevMode(true);
  }

  CrosSystemFake* cros_system_;
  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir;
  base::FilePath vpd_dir;
  base::FilePath vpd_file;
  base::FilePath dev_mode_file;
  startup::FakePlatform* platform_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
};

TEST_F(DevCheckBlockTest, DevSWBoot) {
  ASSERT_TRUE(cros_system_->SetInt("devsw_boot", 0));
  ASSERT_TRUE(cros_system_->SetInt("debug_build", 0));
  ASSERT_TRUE(cros_system_->SetInt("recovery_reason", 0));
  ASSERT_TRUE(CreateDirAndWriteFile(vpd_file, "1"));
  struct stat st;
  st.st_mode = S_IFREG;
  platform_->SetStatResultForPath(vpd_file, st);

  startup_->DevCheckBlockDevMode(dev_mode_file);
  EXPECT_EQ(PathExists(dev_mode_file), false);
}

TEST_F(DevCheckBlockTest, SysFsVpdSlow) {
  ASSERT_TRUE(cros_system_->SetInt("devsw_boot", 1));
  ASSERT_TRUE(cros_system_->SetInt("debug_build", 0));
  ASSERT_TRUE(cros_system_->SetInt("recovery_reason", 0));
  ASSERT_TRUE(CreateDirAndWriteFile(vpd_file, "1"));
  struct stat st;
  st.st_mode = S_IFREG;
  platform_->SetStatResultForPath(vpd_file, st);

  startup_->DevCheckBlockDevMode(dev_mode_file);
  EXPECT_EQ(PathExists(dev_mode_file), true);
}

TEST_F(DevCheckBlockTest, CrosSysBlockDev) {
  ASSERT_TRUE(cros_system_->SetInt("devsw_boot", 1));
  ASSERT_TRUE(cros_system_->SetInt("debug_build", 0));
  ASSERT_TRUE(cros_system_->SetInt("recovery_reason", 0));
  ASSERT_TRUE(cros_system_->SetInt("block_devmode", 1));

  startup_->DevCheckBlockDevMode(dev_mode_file);
  EXPECT_EQ(PathExists(dev_mode_file), true);
}

TEST_F(DevCheckBlockTest, ReadVpdSlowFail) {
  ASSERT_TRUE(cros_system_->SetInt("devsw_boot", 1));
  ASSERT_TRUE(cros_system_->SetInt("debug_build", 0));
  ASSERT_TRUE(cros_system_->SetInt("recovery_reason", 0));
  ASSERT_TRUE(cros_system_->SetInt("block_devmode", 0));
  ASSERT_TRUE(cros_system_->SetInt("nvram_cleared", 1));

  platform_->SetVpdResult(-1);

  startup_->DevCheckBlockDevMode(dev_mode_file);
  EXPECT_EQ(PathExists(dev_mode_file), false);
}

TEST_F(DevCheckBlockTest, ReadVpdSlowPass) {
  ASSERT_TRUE(cros_system_->SetInt("devsw_boot", 1));
  ASSERT_TRUE(cros_system_->SetInt("debug_build", 0));
  ASSERT_TRUE(cros_system_->SetInt("recovery_reason", 0));
  ASSERT_TRUE(cros_system_->SetInt("block_devmode", 0));
  ASSERT_TRUE(cros_system_->SetInt("nvram_cleared", 1));

  platform_->SetVpdResult(1);
  std::string res;
  std::vector<std::string> args = {"args"};
  platform_->VpdSlow(args, &res);
  EXPECT_EQ(res, "1");

  startup_->DevCheckBlockDevMode(dev_mode_file);
  EXPECT_EQ(PathExists(dev_mode_file), true);
  EXPECT_EQ(platform_->GetBootAlertForArg("block_devmode"), 1);
}

class TPMTest : public ::testing::Test {
 protected:
  TPMTest() : cros_system_(new CrosSystemFake()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir = temp_dir_.GetPath();
    platform_ = new startup::FakePlatform();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::unique_ptr<CrosSystem>(cros_system_), flags_, base_dir, base_dir,
        base_dir, base_dir, std::unique_ptr<startup::FakePlatform>(platform_),
        std::make_unique<startup::MountHelper>(
            std::make_unique<startup::FakePlatform>(), flags_, base_dir,
            base_dir));
  }

  CrosSystemFake* cros_system_;
  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir;
  startup::FakePlatform* platform_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
};

TEST_F(TPMTest, OwnedFileTrue) {
  base::FilePath tpm_file = base_dir.Append("sys/class/tpm/tmp0/device/owned");
  ASSERT_TRUE(CreateDirAndWriteFile(tpm_file, "1"));
  EXPECT_EQ(startup_->IsTPMOwned(), true);
}

TEST_F(TPMTest, OwnedFileFalse) {
  base::FilePath tpm_file = base_dir.Append("sys/class/tpm/tmp0/device/owned");
  ASSERT_TRUE(CreateDirAndWriteFile(tpm_file, "0"));
  EXPECT_EQ(startup_->IsTPMOwned(), false);
}

TEST_F(TPMTest, NeedsClobberTPMOwned) {
  base::FilePath tpm_file = base_dir.Append("sys/class/tpm/tmp0/device/owned");
  ASSERT_TRUE(CreateDirAndWriteFile(tpm_file, "1"));
  EXPECT_EQ(startup_->IsTPMOwned(), true);
  EXPECT_EQ(startup_->NeedsClobberWithoutDevModeFile(), false);
}

TEST_F(TPMTest, NeedsClobberPreservationFile) {
  base::FilePath tpm_file = base_dir.Append("sys/class/tpm/tmp0/device/owned");
  ASSERT_TRUE(CreateDirAndWriteFile(tpm_file, "0"));
  EXPECT_EQ(startup_->IsTPMOwned(), false);
  base::FilePath preservation_file = base_dir.Append("preservation_request");
  ASSERT_TRUE(CreateDirAndWriteFile(preservation_file, "0"));
  struct stat st;
  st.st_uid = -1;
  platform_->SetStatResultForPath(preservation_file, st);
  EXPECT_EQ(startup_->NeedsClobberWithoutDevModeFile(), false);
}

TEST_F(TPMTest, NeedsClobberInstallFile) {
  base::FilePath tpm_file = base_dir.Append("sys/class/tpm/tmp0/device/owned");
  ASSERT_TRUE(CreateDirAndWriteFile(tpm_file, "0"));
  EXPECT_EQ(startup_->IsTPMOwned(), false);
  base::FilePath preservation_file = base_dir.Append("preservation_request");
  ASSERT_TRUE(CreateDirAndWriteFile(preservation_file, "0"));
  struct stat st;
  st.st_uid = -1;
  platform_->SetStatResultForPath(preservation_file, st);
  base::FilePath install_file =
      base_dir.Append("home/.shadow/install_attributes.pb");
  ASSERT_TRUE(CreateDirAndWriteFile(install_file, "0"));
  LOG(INFO) << "test getuid " << getuid();
  st.st_uid = getuid();
  platform_->SetStatResultForPath(install_file, st);
  EXPECT_EQ(startup_->NeedsClobberWithoutDevModeFile(), true);
}

class TpmCleanupTest : public ::testing::Test {
 protected:
  TpmCleanupTest() : cros_system_(new CrosSystemFake()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir = temp_dir_.GetPath();
    mock_platform_ = new startup::MockPlatform();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::unique_ptr<CrosSystem>(cros_system_), flags_, base_dir, base_dir,
        base_dir, base_dir,
        std::unique_ptr<startup::MockPlatform>(mock_platform_),
        std::make_unique<startup::MountHelper>(
            std::make_unique<startup::FakePlatform>(), flags_, base_dir,
            base_dir));
    flag_file_ = base_dir.Append(kTpmFirmwareUpdateRequestFlagFile);
    tpm_cleanup_ = base_dir.Append(kTpmFirmwareUpdateCleanup);
  }

  CrosSystemFake* cros_system_;
  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir;
  startup::MockPlatform* mock_platform_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
  base::FilePath flag_file_;
  base::FilePath tpm_cleanup_;
};

TEST_F(TpmCleanupTest, TpmCleanupNoFlagFile) {
  EXPECT_CALL(*mock_platform_, RunProcess(tpm_cleanup_)).Times(0);
  startup_->CleanupTpm();
}

TEST_F(TpmCleanupTest, TpmCleanupNoCmdPath) {
  CreateDirAndWriteFile(flag_file_, "exists");
  EXPECT_CALL(*mock_platform_, RunProcess(tpm_cleanup_)).Times(0);
  startup_->CleanupTpm();
}

TEST_F(TpmCleanupTest, TpmCleanupSuccess) {
  CreateDirAndWriteFile(flag_file_, "exists");
  CreateDirAndWriteFile(tpm_cleanup_, "exists");
  EXPECT_CALL(*mock_platform_, RunProcess(tpm_cleanup_)).Times(1);
  startup_->CleanupTpm();
}

class MountStackTest : public ::testing::Test {
 protected:
  MountStackTest()
      : base_dir_(base::FilePath("")),
        platform_(new startup::FakePlatform()),
        mount_helper_(std::unique_ptr<startup::FakePlatform>(platform_),
                      flags_,
                      base_dir_,
                      base_dir_) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
  }

  startup::Flags flags_;
  base::FilePath base_dir_;
  base::ScopedTempDir temp_dir_;
  startup::FakePlatform* platform_;
  startup::MountHelper mount_helper_;
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

}  // namespace startup
