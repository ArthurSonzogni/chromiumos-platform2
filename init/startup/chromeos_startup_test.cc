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
#include "init/startup/constants.h"
#include "init/startup/factory_mode_mount_helper.h"
#include "init/startup/fake_platform_impl.h"
#include "init/startup/flags.h"
#include "init/startup/mock_platform_impl.h"
#include "init/startup/mount_helper.h"
#include "init/startup/mount_helper_factory.h"
#include "init/startup/platform_impl.h"
#include "init/startup/standard_mount_helper.h"
#include "init/startup/test_mode_mount_helper.h"

using testing::_;
using testing::AnyNumber;
using testing::ByMove;
using testing::Return;
using testing::StrictMock;

namespace startup {

namespace {

constexpr char kTpmFirmwareUpdateCleanup[] =
    "usr/sbin/tpm-firmware-update-cleanup";
constexpr char kTpmFirmwareUpdateRequestFlagFile[] =
    "unencrypted/preserve/tpm_firmware_update_request";
constexpr char kLsbRelease[] = "lsb-release";
constexpr char kStatefulPartition[] = "mnt/stateful_partition";
constexpr char kProcCmdLine[] = "proc/cmdline";
constexpr char kSysKeyLog[] = "run/create_system_key.log";
constexpr char kMntOptionsFile[] =
    "dev_image/factory/init/encstateful_mount_option";

// Helper function to create directory and write to file.
bool CreateDirAndWriteFile(const base::FilePath& path,
                           const std::string& contents) {
  return base::CreateDirectory(path.DirName()) &&
         base::WriteFile(path, contents.c_str(), contents.length()) ==
             contents.length();
}

void CreateBaseAndSetNames(base::FilePath* base_dir,
                           base::FilePath* lsb_file,
                           base::FilePath* stateful) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  *base_dir = temp_dir.GetPath();
  *lsb_file = base_dir->Append(kLsbRelease);
  *stateful = base_dir->Append(kStatefulPartition);
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
    security_hardening_ = base_dir_.Append(
        "usr/share/cros/startup/disable_stateful_security_hardening");
    kernel_security_ = base_dir_.Append("sys/kernel/security");
    namespaces_ = base_dir_.Append("run/namespaces");
    platform_ = new startup::FakePlatform();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::unique_ptr<CrosSystem>(cros_system_), flags_, base_dir_, base_dir_,
        base_dir_, base_dir_, std::unique_ptr<startup::FakePlatform>(platform_),
        std::make_unique<startup::StandardMountHelper>(
            std::make_unique<startup::FakePlatform>(), flags_, base_dir_,
            base_dir_, true));
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
  base::FilePath security_hardening_;
  base::FilePath kernel_security_;
  base::FilePath namespaces_;
};

TEST_F(EarlySetupTest, EarlySetup) {
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
        std::make_unique<startup::StandardMountHelper>(
            std::make_unique<startup::FakePlatform>(), flags_, base_dir,
            base_dir, true));
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

class MaybeMountEfivarfsTest : public ::testing::Test {
 protected:
  MaybeMountEfivarfsTest() : cros_system_(new CrosSystemFake()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    efivars_dir_ = base_dir_.Append("sys/firmware/efi/efivars");
    filesystems_file_ = base_dir_.Append("proc/filesystems");
    mock_platform_ = std::make_unique<StrictMock<startup::MockPlatform>>();
  }

  // Extra layer of safety to ensure all tests call `MakeStartup`.
  void TearDown() override { startup_.reset(); }

  // Move `ChromeosStartup` creation out of SetUp to give access to the
  // `MockPlatform`. After calling this function `mock_platform_` will no longer
  // be usable.
  void MakeStartup() {
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::unique_ptr<CrosSystem>(cros_system_), flags_, base_dir_, base_dir_,
        base_dir_, base_dir_, std::move(mock_platform_),
        std::make_unique<startup::StandardMountHelper>(
            std::make_unique<startup::FakePlatform>(), flags_, base_dir_,
            base_dir_, true));
  }

  CrosSystemFake* cros_system_;
  startup::Flags flags_;
  std::unique_ptr<startup::MockPlatform> mock_platform_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
  base::FilePath efivars_dir_;
  base::FilePath filesystems_file_;
};

TEST_F(MaybeMountEfivarfsTest, Supported) {
  ASSERT_TRUE(CreateDirAndWriteFile(filesystems_file_, ("nodev\tsysfs\n"
                                                        "nodev\tefivarfs\n"
                                                        "\text4\n")));

  EXPECT_CALL(*mock_platform_, Mount("efivarfs", efivars_dir_, "efivarfs",
                                     kCommonMountFlags, _));

  MakeStartup();

  startup_->MaybeMountEfivarfs();
}

TEST_F(MaybeMountEfivarfsTest, NotSupported) {
  ASSERT_TRUE(CreateDirAndWriteFile(filesystems_file_, ("nodev\tsysfs\n"
                                                        "\text4\n")));

  // Expect that we mount nothing.
  // Typed Matcher supplied to disambiguate between Mount calls for complier.
  // Somewhat brittle, as a new version of the mount call could be introduced
  // and used without breaking this, but seems like the best gTest gives us?
  EXPECT_CALL(*mock_platform_,
              Mount(testing::Matcher<const std::string&>(_), _, _, _, _))
      .Times(0);
  EXPECT_CALL(*mock_platform_,
              Mount(testing::Matcher<const base::FilePath&>(_), _, _, _, _))
      .Times(0);

  MakeStartup();

  startup_->MaybeMountEfivarfs();
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
        std::make_unique<startup::StandardMountHelper>(
            std::make_unique<startup::FakePlatform>(), flags_, base_dir,
            base_dir, false));
  }

  CrosSystemFake* cros_system_;
  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir;
  startup::FakePlatform* platform_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
};

TEST_F(TPMTest, OwnedFileTrue) {
  base::FilePath tpm_file = base_dir.Append(kTPMOwnedPath);
  ASSERT_TRUE(CreateDirAndWriteFile(tpm_file, "1"));
  EXPECT_EQ(startup_->IsTPMOwned(), true);
}

TEST_F(TPMTest, OwnedFileFalse) {
  base::FilePath tpm_file = base_dir.Append(kTPMOwnedPath);
  ASSERT_TRUE(CreateDirAndWriteFile(tpm_file, "0"));
  EXPECT_EQ(startup_->IsTPMOwned(), false);
}

TEST_F(TPMTest, NeedsClobberTPMOwned) {
  base::FilePath tpm_file = base_dir.Append(kTPMOwnedPath);
  ASSERT_TRUE(CreateDirAndWriteFile(tpm_file, "1"));
  EXPECT_EQ(startup_->IsTPMOwned(), true);
  EXPECT_EQ(startup_->NeedsClobberWithoutDevModeFile(), false);
}

TEST_F(TPMTest, NeedsClobberPreservationFile) {
  base::FilePath tpm_file = base_dir.Append(kTPMOwnedPath);
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
  base::FilePath tpm_file = base_dir.Append(kTPMOwnedPath);
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
        std::make_unique<startup::StandardMountHelper>(
            std::make_unique<startup::FakePlatform>(), flags_, base_dir,
            base_dir, true));
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

class ConfigTest : public ::testing::Test {
 protected:
  ConfigTest() {}

  void SetUp() override {
    cros_system_ = std::make_unique<CrosSystemFake>();
    CreateBaseAndSetNames(&base_dir_, &lsb_file_, &stateful_);
    platform_ = std::make_unique<startup::FakePlatform>();
  }

  std::unique_ptr<startup::MountHelper> GenerateMountHelper() {
    startup::Flags flags;
    startup::ChromeosStartup::ParseFlags(&flags);
    startup::MountHelperFactory factory(std::move(platform_),
                                        cros_system_.get(), flags, base_dir_,
                                        stateful_, lsb_file_);
    return factory.Generate();
  }

  std::unique_ptr<CrosSystemFake> cros_system_;
  base::FilePath base_dir_;
  base::FilePath lsb_file_;
  base::FilePath stateful_;
  std::unique_ptr<startup::FakePlatform> platform_;
};

TEST_F(ConfigTest, NoDevMode) {
  ASSERT_TRUE(cros_system_->SetInt("cros_debug", 0));
  ASSERT_TRUE(CreateDirAndWriteFile(lsb_file_,
                                    "CHROMEOS_RELEASE_TRACK=stable-channel\n"));
  std::unique_ptr<startup::MountHelper> helper = GenerateMountHelper();
  EXPECT_EQ(helper->GetMountHelperType(),
            startup::MountHelperType::kStandardMode);
}

TEST_F(ConfigTest, DevMode) {
  ASSERT_TRUE(cros_system_->SetInt("cros_debug", 1));
  ASSERT_TRUE(CreateDirAndWriteFile(lsb_file_,
                                    "CHROMEOS_RELEASE_TRACK=stable-channel\n"));
  std::unique_ptr<startup::MountHelper> helper = GenerateMountHelper();
  EXPECT_EQ(helper->GetMountHelperType(),
            startup::MountHelperType::kStandardMode);
}

TEST_F(ConfigTest, DevModeTest) {
  ASSERT_TRUE(cros_system_->SetInt("cros_debug", 1));
  ASSERT_TRUE(cros_system_->SetInt("debug_build", 0));
  ASSERT_TRUE(CreateDirAndWriteFile(
      lsb_file_, "CHROMEOS_RELEASE_TRACK=testimage-channel\n"));
  std::unique_ptr<startup::MountHelper> helper = GenerateMountHelper();
  EXPECT_EQ(helper->GetMountHelperType(), startup::MountHelperType::kTestMode);
}

TEST_F(ConfigTest, DevModeTestFactoryTest) {
  ASSERT_TRUE(cros_system_->SetInt("cros_debug", 1));
  ASSERT_TRUE(cros_system_->SetInt("debug_build", 1));
  ASSERT_TRUE(CreateDirAndWriteFile(
      lsb_file_, "CHROMEOS_RELEASE_TRACK=testimage-channel\n"));
  base::FilePath factory_en = stateful_.Append("dev_image/factory/enabled");
  ASSERT_TRUE(CreateDirAndWriteFile(factory_en, "Enabled"));
  std::unique_ptr<startup::MountHelper> helper = GenerateMountHelper();
  EXPECT_EQ(helper->GetMountHelperType(),
            startup::MountHelperType::kFactoryMode);
}

TEST_F(ConfigTest, DevModeTestFactoryInstaller) {
  ASSERT_TRUE(cros_system_->SetInt("cros_debug", 1));
  ASSERT_TRUE(cros_system_->SetInt("debug_build", 0));
  ASSERT_TRUE(CreateDirAndWriteFile(
      lsb_file_, "CHROMEOS_RELEASE_TRACK=testimage-channel\n"));
  base::FilePath cmdline = base_dir_.Append(kProcCmdLine);
  ASSERT_TRUE(CreateDirAndWriteFile(cmdline, "cros_factory_install"));
  std::unique_ptr<startup::MountHelper> helper = GenerateMountHelper();
  EXPECT_EQ(helper->GetMountHelperType(),
            startup::MountHelperType::kFactoryMode);
}

TEST_F(ConfigTest, DevModeTestFactoryInstallerUsingFile) {
  ASSERT_TRUE(cros_system_->SetInt("cros_debug", 1));
  ASSERT_TRUE(cros_system_->SetInt("debug_build", 0));
  ASSERT_TRUE(CreateDirAndWriteFile(
      lsb_file_, "CHROMEOS_RELEASE_TRACK=testimage-channel\n"));
  base::FilePath cmdline = base_dir_.Append(kProcCmdLine);
  ASSERT_TRUE(CreateDirAndWriteFile(cmdline, "not_factory_install"));
  base::FilePath installer = base_dir_.Append("root/.factory_installer");
  ASSERT_TRUE(CreateDirAndWriteFile(installer, "factory"));
  std::unique_ptr<startup::MountHelper> helper = GenerateMountHelper();
  EXPECT_EQ(helper->GetMountHelperType(),
            startup::MountHelperType::kFactoryMode);
}

class MountStackTest : public ::testing::Test {
 protected:
  MountStackTest()
      : base_dir_(base::FilePath("")),
        platform_(new startup::FakePlatform()),
        mount_helper_(std::unique_ptr<startup::FakePlatform>(platform_),
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
  startup::FakePlatform* platform_;
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

  std::unique_ptr<startup::FakePlatform> platform_ =
      std::make_unique<startup::FakePlatform>();
  platform_->SetMountEncOutputForArg("", "1");
  std::unique_ptr<startup::StandardMountHelper> mount_helper_ =
      std::make_unique<startup::StandardMountHelper>(
          std::move(platform_), flags_, base_dir_, base_dir_, true);

  bool res = mount_helper_->MountVarAndHomeChronosEncrypted();
  EXPECT_EQ(res, true);
}

TEST(MountVarAndHomeChronosEncrypted, MountEncryptedFail) {
  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
  base::FilePath base_dir_ = temp_dir_.GetPath();

  std::unique_ptr<startup::FakePlatform> platform_ =
      std::make_unique<startup::FakePlatform>();
  std::unique_ptr<startup::StandardMountHelper> mount_helper_ =
      std::make_unique<startup::StandardMountHelper>(
          std::move(platform_), flags_, base_dir_, base_dir_, true);

  bool res = mount_helper_->MountVarAndHomeChronosEncrypted();
  EXPECT_EQ(res, false);
}

class DoMountTest : public ::testing::Test {
 protected:
  DoMountTest() {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    platform_ = std::make_unique<startup::FakePlatform>();
  }

  startup::Flags flags_;
  base::FilePath base_dir_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<startup::FakePlatform> platform_;
};

TEST_F(DoMountTest, StandardMountHelper) {
  flags_.encstateful = true;
  platform_->SetMountEncOutputForArg("", "1");
  std::unique_ptr<startup::StandardMountHelper> mount_helper_ =
      std::make_unique<startup::StandardMountHelper>(
          std::move(platform_), flags_, base_dir_, base_dir_, true);
  bool res = mount_helper_->DoMountVarAndHomeChronos();
  EXPECT_EQ(res, true);
}

TEST_F(DoMountTest, TestModeMountHelperCreateSystemKey) {
  flags_.sys_key_util = true;
  flags_.encstateful = true;
  base::FilePath no_early = base_dir_.Append(".no_early_system_key");
  base::FilePath log_file = base_dir_.Append(kSysKeyLog);
  ASSERT_TRUE(CreateDirAndWriteFile(no_early, "1"));
  ASSERT_TRUE(CreateDirAndWriteFile(log_file, "1"));

  struct stat st;
  st.st_mode = S_IFREG;
  platform_->SetStatResultForPath(no_early, st);

  platform_->SetMountEncOutputForArg("", "1");
  std::unique_ptr<startup::TestModeMountHelper> mount_helper_ =
      std::make_unique<startup::TestModeMountHelper>(
          std::move(platform_), flags_, base_dir_, base_dir_, true);
  bool res = mount_helper_->DoMountVarAndHomeChronos();
  EXPECT_EQ(res, true);
  std::string sys_key_log_out;
  base::ReadFileToString(log_file, &sys_key_log_out);
  EXPECT_EQ(sys_key_log_out, "Opt not to create a system key in advance.");
}

TEST_F(DoMountTest, TestModeMountHelperMountEncryptFailed) {
  flags_.sys_key_util = false;
  flags_.encstateful = true;
  base::FilePath mnt_encrypt_fail = base_dir_.Append("mount_encrypted_failed");

  struct stat st;
  st.st_uid = -1;
  platform_->SetStatResultForPath(mnt_encrypt_fail, st);

  platform_->SetMountEncOutputForArg("", "1");
  std::unique_ptr<startup::TestModeMountHelper> mount_helper_ =
      std::make_unique<startup::TestModeMountHelper>(
          std::move(platform_), flags_, base_dir_, base_dir_, true);
  bool res = mount_helper_->DoMountVarAndHomeChronos();
  EXPECT_EQ(res, true);
}

TEST_F(DoMountTest, TestModeMountHelperMountVarSuccess) {
  flags_.sys_key_util = false;
  flags_.encstateful = true;
  base::FilePath clobber_log_ = base_dir_.Append("clobber_test_log");
  platform_->SetClobberLogFile(clobber_log_);

  platform_->SetMountEncOutputForArg("", "1");
  std::unique_ptr<startup::TestModeMountHelper> mount_helper_ =
      std::make_unique<startup::TestModeMountHelper>(
          std::move(platform_), flags_, base_dir_, base_dir_, true);
  bool res = mount_helper_->DoMountVarAndHomeChronos();
  EXPECT_EQ(res, true);
  std::string clobber_log_out;
  base::ReadFileToString(clobber_log_, &clobber_log_out);
  EXPECT_EQ(clobber_log_out, "");
}

TEST_F(DoMountTest, TestModeMountHelperMountVarFail) {
  flags_.sys_key_util = false;
  flags_.encstateful = true;
  base::FilePath clobber_log_ = base_dir_.Append("clobber_test_log");
  platform_->SetClobberLogFile(clobber_log_);
  base::FilePath mnt_encrypt_fail = base_dir_.Append("mount_encrypted_failed");
  struct stat st;
  st.st_uid = getuid();
  platform_->SetStatResultForPath(mnt_encrypt_fail, st);
  base::FilePath corrupted_enc = base_dir_.Append("corrupted_encryption");
  base::FilePath encrypted_test = base_dir_.Append("encrypted.test1");
  base::FilePath encrypted_test2 = base_dir_.Append("encrypted.test2");
  ASSERT_TRUE(CreateDirAndWriteFile(encrypted_test, "1"));
  ASSERT_TRUE(CreateDirAndWriteFile(encrypted_test2, "1"));

  std::unique_ptr<startup::TestModeMountHelper> mount_helper_ =
      std::make_unique<startup::TestModeMountHelper>(
          std::move(platform_), flags_, base_dir_, base_dir_, true);
  bool res = mount_helper_->DoMountVarAndHomeChronos();
  EXPECT_EQ(res, false);
  std::string clobber_log_out;
  base::ReadFileToString(clobber_log_, &clobber_log_out);
  EXPECT_EQ(clobber_log_out,
            "Failed mounting var and home/chronos; re-created.");
  EXPECT_EQ(base::PathExists(corrupted_enc.Append("encrypted.test1")), true);
  EXPECT_EQ(base::PathExists(corrupted_enc.Append("encrypted.test2")), true);
}

TEST_F(DoMountTest, FactoryModeMountHelperTmpfsFailMntVar) {
  flags_.encstateful = true;
  base::FilePath options_file = base_dir_.Append(kMntOptionsFile);
  ASSERT_TRUE(CreateDirAndWriteFile(options_file, "tmpfs"));

  std::unique_ptr<startup::FactoryModeMountHelper> mount_helper_ =
      std::make_unique<startup::FactoryModeMountHelper>(
          std::move(platform_), flags_, base_dir_, base_dir_, true);
  bool res = mount_helper_->DoMountVarAndHomeChronos();
  EXPECT_EQ(res, false);
}

TEST_F(DoMountTest, FactoryModeMountHelperTmpfsFailMntHomeChronos) {
  flags_.encstateful = true;
  base::FilePath options_file = base_dir_.Append(kMntOptionsFile);
  ASSERT_TRUE(CreateDirAndWriteFile(options_file, "tmpfs"));
  base::FilePath tmpfs_var = base::FilePath("tmpfs_var");
  base::FilePath var = base_dir_.Append("var");
  platform_->SetMountResultForPath(var, tmpfs_var.value());
  base::FilePath stateful_home_chronos = base_dir_.Append("home/chronos");
  platform_->SetMountResultForPath(stateful_home_chronos, "fail");

  std::unique_ptr<startup::FactoryModeMountHelper> mount_helper_ =
      std::make_unique<startup::FactoryModeMountHelper>(
          std::move(platform_), flags_, base_dir_, base_dir_, true);
  bool res = mount_helper_->DoMountVarAndHomeChronos();
  EXPECT_EQ(res, false);
}

TEST_F(DoMountTest, FactoryModeMountHelperTmpfsSuccess) {
  flags_.encstateful = true;
  base::FilePath stateful = base_dir_.Append("mnt/stateful_partition");
  base::FilePath options_file = stateful.Append(kMntOptionsFile);
  ASSERT_TRUE(CreateDirAndWriteFile(options_file, "tmpfs"));
  base::FilePath tmpfs_var = base::FilePath("tmpfs_var");
  base::FilePath var = base_dir_.Append("var");
  platform_->SetMountResultForPath(var, tmpfs_var.value());
  base::FilePath stateful_home_chronos = stateful.Append("home/chronos");
  base::FilePath home_chronos = base_dir_.Append("home/chronos");
  platform_->SetMountResultForPath(home_chronos, stateful_home_chronos.value());

  std::unique_ptr<startup::FactoryModeMountHelper> mount_helper_ =
      std::make_unique<startup::FactoryModeMountHelper>(
          std::move(platform_), flags_, base_dir_, stateful, true);
  bool res = mount_helper_->DoMountVarAndHomeChronos();
  EXPECT_EQ(res, true);
}

TEST_F(DoMountTest, FactoryModeMountHelperUnencryptFailMntVar) {
  flags_.encstateful = true;
  base::FilePath stateful = base_dir_.Append("mnt/stateful_partition");
  // base::FilePath stateful_var = stateful.Append("var");
  // platform_->SetMountResultForPath(stateful_var, "fail");

  std::unique_ptr<startup::FactoryModeMountHelper> mount_helper_ =
      std::make_unique<startup::FactoryModeMountHelper>(
          std::move(platform_), flags_, base_dir_, stateful, true);
  bool res = mount_helper_->DoMountVarAndHomeChronos();
  EXPECT_EQ(res, false);
}

TEST_F(DoMountTest, FactoryModeMountHelperUnencryptFailMntHomeChronos) {
  flags_.encstateful = true;
  base::FilePath stateful = base_dir_.Append("mnt/stateful_partition");
  base::FilePath stateful_var = stateful.Append("var");
  base::FilePath var = base_dir_.Append("var");
  platform_->SetMountResultForPath(var, stateful_var.value());
  // base::FilePath stateful_home_chronos = stateful.Append("home/chronos");
  // platform_->SetMountResultForPath(stateful_home_chronos, "fail");

  std::unique_ptr<startup::FactoryModeMountHelper> mount_helper_ =
      std::make_unique<startup::FactoryModeMountHelper>(
          std::move(platform_), flags_, base_dir_, stateful, true);
  bool res = mount_helper_->DoMountVarAndHomeChronos();
  EXPECT_EQ(res, false);
}

TEST_F(DoMountTest, FactoryModeMountHelperUnencryptSuccess) {
  flags_.encstateful = true;
  base::FilePath stateful = base_dir_.Append("mnt/stateful_partition");
  base::FilePath stateful_var = stateful.Append("var");
  base::FilePath var = base_dir_.Append("var");
  platform_->SetMountResultForPath(var, stateful_var.value());
  base::FilePath stateful_home_chronos = stateful.Append("home/chronos");
  base::FilePath home_chronos = base_dir_.Append("home/chronos");
  platform_->SetMountResultForPath(home_chronos, stateful_home_chronos.value());

  std::unique_ptr<startup::FactoryModeMountHelper> mount_helper_ =
      std::make_unique<startup::FactoryModeMountHelper>(
          std::move(platform_), flags_, base_dir_, stateful, true);
  bool res = mount_helper_->DoMountVarAndHomeChronos();
  EXPECT_EQ(res, true);
}

class IsVarFullTest : public ::testing::Test {
 protected:
  IsVarFullTest() : cros_system_(new CrosSystemFake()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir = temp_dir_.GetPath();
    platform_ = new startup::FakePlatform();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::unique_ptr<CrosSystem>(cros_system_), flags_, base_dir, base_dir,
        base_dir, base_dir, std::unique_ptr<startup::FakePlatform>(platform_),
        std::make_unique<startup::StandardMountHelper>(
            std::make_unique<startup::FakePlatform>(), flags_, base_dir,
            base_dir, false));
  }

  CrosSystemFake* cros_system_;
  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir;
  startup::FakePlatform* platform_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
};

TEST_F(IsVarFullTest, StatvfsFailure) {
  bool res = startup_->IsVarFull();
  EXPECT_EQ(res, false);
}

TEST_F(IsVarFullTest, Failure) {
  base::FilePath var = base_dir.Append("var");

  struct statvfs st;
  st.f_bavail = 11000;
  st.f_favail = 110;
  platform_->SetStatvfsResultForPath(var, st);

  bool res = startup_->IsVarFull();
  EXPECT_EQ(res, false);
}

TEST_F(IsVarFullTest, TrueBavail) {
  base::FilePath var = base_dir.Append("var");

  struct statvfs st;
  st.f_bavail = 9000;
  st.f_favail = 110;
  platform_->SetStatvfsResultForPath(var, st);

  bool res = startup_->IsVarFull();
  EXPECT_EQ(res, true);
}

TEST_F(IsVarFullTest, TrueFavail) {
  base::FilePath var = base_dir.Append("var");

  struct statvfs st;
  st.f_bavail = 11000;
  st.f_favail = 90;
  platform_->SetStatvfsResultForPath(var, st);

  bool res = startup_->IsVarFull();
  EXPECT_EQ(res, true);
}

class DeviceSettingsTest : public ::testing::Test {
 protected:
  DeviceSettingsTest() : cros_system_(new CrosSystemFake()) {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir = temp_dir_.GetPath();
    platform_ = new startup::FakePlatform();
    startup_ = std::make_unique<startup::ChromeosStartup>(
        std::unique_ptr<CrosSystem>(cros_system_), flags_, base_dir, base_dir,
        base_dir, base_dir, std::unique_ptr<startup::FakePlatform>(platform_),
        std::make_unique<startup::StandardMountHelper>(
            std::make_unique<startup::FakePlatform>(), flags_, base_dir,
            base_dir, false));
    base::FilePath var_lib = base_dir.Append("var/lib");
    whitelist_ = var_lib.Append("whitelist");
    devicesettings_ = var_lib.Append("devicesettings");
  }

  CrosSystemFake* cros_system_;
  startup::Flags flags_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir;
  startup::FakePlatform* platform_;
  std::unique_ptr<startup::ChromeosStartup> startup_;
  base::FilePath whitelist_;
  base::FilePath devicesettings_;
};

TEST_F(DeviceSettingsTest, OldPathEmpty) {
  ASSERT_TRUE(base::CreateDirectory(whitelist_));
  base::FilePath devicesettings_test = devicesettings_.Append("test");
  ASSERT_TRUE(CreateDirAndWriteFile(devicesettings_test, "test"));

  startup_->MoveToLibDeviceSettings();
  EXPECT_EQ(base::DirectoryExists(whitelist_), false);
  EXPECT_EQ(base::PathExists(devicesettings_test), true);
}

TEST_F(DeviceSettingsTest, NewPathEmpty) {
  ASSERT_TRUE(base::CreateDirectory(whitelist_));
  ASSERT_TRUE(base::CreateDirectory(devicesettings_));
  base::FilePath whitelist_test = whitelist_.Append("test");
  ASSERT_TRUE(CreateDirAndWriteFile(whitelist_test, "test"));
  base::FilePath devicesettings_test = devicesettings_.Append("test");

  startup_->MoveToLibDeviceSettings();
  EXPECT_EQ(base::DirectoryExists(whitelist_), false);
  EXPECT_EQ(base::PathExists(whitelist_test), false);
  EXPECT_EQ(base::PathExists(devicesettings_test), true);
}

TEST_F(DeviceSettingsTest, NeitherPathEmpty) {
  ASSERT_TRUE(base::CreateDirectory(whitelist_));
  ASSERT_TRUE(base::CreateDirectory(devicesettings_));
  base::FilePath whitelist_test = whitelist_.Append("test_w");
  ASSERT_TRUE(CreateDirAndWriteFile(whitelist_test, "test_w"));
  base::FilePath devicesettings_test = devicesettings_.Append("test_d");
  ASSERT_TRUE(CreateDirAndWriteFile(devicesettings_test, "test_d"));

  startup_->MoveToLibDeviceSettings();
  EXPECT_EQ(base::DirectoryExists(whitelist_), true);
  EXPECT_EQ(base::PathExists(whitelist_test), true);
  EXPECT_EQ(base::PathExists(devicesettings_test), true);
}

}  // namespace startup
