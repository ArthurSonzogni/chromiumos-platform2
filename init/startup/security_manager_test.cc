// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdlib.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/values.h>
#include <brillo/files/file_util.h>
#include <gtest/gtest.h>
#include <libstorage/platform/mock_platform.h>
#include <libstorage/platform/platform.h>
#include <linux/loadpin.h>

#include "init/startup/fake_startup_dep_impl.h"
#include "init/startup/mock_startup_dep_impl.h"
#include "init/startup/security_manager.h"
#include "init/startup/startup_dep_impl.h"

using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::StrictMock;

namespace {

constexpr char kStatefulPartition[] = "mnt/stateful_partition";
constexpr char kPreserveSysKeyFile[] = "unencrypted/preserve/system.key";

MATCHER_P(IntPtrCheck, expected, "") {
  return *arg == expected;
}

bool ExceptionsTestFunc(libstorage::Platform* platform,
                        const base::FilePath& root,
                        const std::string& path) {
  base::FilePath allow = root.Append("allow_file");
  std::string data;
  platform->ReadFileToString(allow, &data);
  data.append(path);
  data.append("\n");
  return platform->WriteStringToFile(allow, data);
}

}  // namespace

class SecurityManagerTest : public ::testing::Test {
 protected:
  SecurityManagerTest() {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    platform_ = std::make_unique<libstorage::MockPlatform>();
    mock_startup_dep_ = std::make_unique<StrictMock<startup::MockStartupDep>>();
  }

  std::unique_ptr<startup::MockStartupDep> mock_startup_dep_;

  std::unique_ptr<libstorage::MockPlatform> platform_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir_;
};

class SecurityManagerLoadPinTest : public SecurityManagerTest {
 protected:
  SecurityManagerLoadPinTest() {}

  void SetUp() override {
    SecurityManagerTest::SetUp();

    loadpin_verity_path_ =
        base_dir_.Append("sys/kernel/security/loadpin/dm-verity");
    platform_->WriteStringToFile(loadpin_verity_path_, kNull);

    trusted_verity_digests_path_ =
        base_dir_.Append("opt/google/dlc/_trusted_verity_digests");
    platform_->WriteStringToFile(trusted_verity_digests_path_, kRootDigest);

    dev_null_path_ = base_dir_.Append("dev/null");
    platform_->WriteStringToFile(dev_null_path_, kNull);
  }

  base::FilePath loadpin_verity_path_;
  base::FilePath trusted_verity_digests_path_;
  base::FilePath dev_null_path_;
  const std::string kRootDigest =
      "fb066d299c657b127ecc2c11f841cabf14c717eb6f03630ef788e6e1cca17f52";
  const std::string kNull = "\0";
};

TEST_F(SecurityManagerTest, After_v4_14) {
  base::FilePath policies_dir =
      base_dir_.Append("usr/share/cros/startup/process_management_policies");
  base::FilePath mgmt_policies =
      base_dir_.Append("sys/kernel/security/safesetid/whitelist_policy");
  ASSERT_TRUE(platform_->WriteStringToFile(mgmt_policies, "#AllowList"));
  base::FilePath allow_1 = policies_dir.Append("allow_1.txt");
  std::string result1 = "254:607\n607:607";
  std::string full1 = "254:607\n607:607\n#Comment\n\n#Ignore";
  ASSERT_TRUE(platform_->WriteStringToFile(allow_1, full1));
  base::FilePath allow_2 = policies_dir.Append("allow_2.txt");
  std::string result2 = "20104:224\n20104:217\n217:217";
  std::string full2 = "#Comment\n\n20104:224\n20104:217\n#Ignore\n217:217";
  ASSERT_TRUE(platform_->WriteStringToFile(allow_2, full2));

  startup::ConfigureProcessMgmtSecurity(platform_.get(), base_dir_);

  std::string allow;
  platform_->ReadFileToString(mgmt_policies, &allow);

  EXPECT_NE(allow.find(result1), std::string::npos);
  EXPECT_NE(allow.find(result2), std::string::npos);

  std::vector<std::string> allow_vec = base::SplitString(
      allow, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  std::vector<std::string> expected = {"254:607", "607:607", "20104:224",
                                       "20104:217", "217:217"};
  sort(allow_vec.begin(), allow_vec.end());
  sort(expected.begin(), expected.end());
  EXPECT_EQ(allow_vec, expected);
}

TEST_F(SecurityManagerTest, After_v5_9) {
  base::FilePath policies_dir =
      base_dir_.Append("usr/share/cros/startup/process_management_policies");
  base::FilePath mgmt_policies =
      base_dir_.Append("sys/kernel/security/safesetid/uid_allowlist_policy");
  ASSERT_TRUE(platform_->WriteStringToFile(mgmt_policies, "#AllowList"));
  base::FilePath allow_1 = policies_dir.Append("allow_1.txt");
  std::string result1 = "254:607\n607:607";
  ASSERT_TRUE(platform_->WriteStringToFile(allow_1, result1));
  base::FilePath allow_2 = policies_dir.Append("allow_2.txt");
  std::string result2 = "20104:224\n20104:217\n217:217";
  ASSERT_TRUE(platform_->WriteStringToFile(allow_2, result2));

  startup::ConfigureProcessMgmtSecurity(platform_.get(), base_dir_);

  std::string allow;
  platform_->ReadFileToString(mgmt_policies, &allow);

  EXPECT_NE(allow.find(result1), std::string::npos);
  EXPECT_NE(allow.find(result2), std::string::npos);
}

TEST_F(SecurityManagerTest, After_v5_9_gid) {
  base::FilePath policies_dir = base_dir_.Append(
      "usr/share/cros/startup/gid_process_management_policies");
  base::FilePath mgmt_policies =
      base_dir_.Append("sys/kernel/security/safesetid/gid_allowlist_policy");
  ASSERT_TRUE(platform_->WriteStringToFile(mgmt_policies, "#AllowList"));
  base::FilePath allow_1 = policies_dir.Append("allow_1.txt");
  std::string result1 = "254:607\n607:607";
  ASSERT_TRUE(platform_->WriteStringToFile(allow_1, result1));
  base::FilePath allow_2 = policies_dir.Append("allow_2.txt");
  std::string result2 = "20104:224\n20104:217\n217:217";
  ASSERT_TRUE(platform_->WriteStringToFile(allow_2, result2));

  startup::ConfigureProcessMgmtSecurity(platform_.get(), base_dir_);

  std::string allow;
  platform_->ReadFileToString(mgmt_policies, &allow);

  EXPECT_NE(allow.find(result1), std::string::npos);
  EXPECT_NE(allow.find(result2), std::string::npos);
}

TEST_F(SecurityManagerTest, EmptyAfter_v5_9) {
  base::FilePath mgmt_policies =
      base_dir_.Append("sys/kernel/security/safesetid/uid_allowlist_policy");
  ASSERT_TRUE(platform_->WriteStringToFile(mgmt_policies, "#AllowList"));

  EXPECT_FALSE(
      startup::ConfigureProcessMgmtSecurity(platform_.get(), base_dir_));

  std::string allow;
  platform_->ReadFileToString(mgmt_policies, &allow);

  EXPECT_EQ(allow, "#AllowList");
}

TEST_F(SecurityManagerLoadPinTest, LoadPinAttributeUnsupported) {
  ASSERT_TRUE(platform_->DeleteFile(loadpin_verity_path_));

  EXPECT_CALL(*platform_, Ioctl(_, LOADPIN_IOC_SET_TRUSTED_VERITY_DIGESTS, _))
      .Times(0);

  // The call should succeed as there is no LoadPin verity file.
  EXPECT_TRUE(startup::SetupLoadPinVerityDigests(platform_.get(), base_dir_,
                                                 mock_startup_dep_.get()));
}

TEST_F(SecurityManagerLoadPinTest, FailureToOpenLoadPinVerity) {
  EXPECT_CALL(*platform_, OpenFile(loadpin_verity_path_, "w"))
      .WillOnce(Return(nullptr));
  EXPECT_CALL(*platform_, Ioctl(_, LOADPIN_IOC_SET_TRUSTED_VERITY_DIGESTS, _))
      .Times(0);

  // The call should fail as failure to open LoadPin verity file.
  EXPECT_FALSE(startup::SetupLoadPinVerityDigests(platform_.get(), base_dir_,
                                                  mock_startup_dep_.get()));
}

TEST_F(SecurityManagerLoadPinTest, ValidDigests) {
  EXPECT_CALL(*platform_, Ioctl(_, LOADPIN_IOC_SET_TRUSTED_VERITY_DIGESTS, _))
      .WillOnce(Return(0));
  EXPECT_TRUE(startup::SetupLoadPinVerityDigests(platform_.get(), base_dir_,
                                                 mock_startup_dep_.get()));
}

TEST_F(SecurityManagerLoadPinTest, MissingDigests) {
  ASSERT_TRUE(platform_->DeleteFile(trusted_verity_digests_path_));
  EXPECT_CALL(*platform_, Ioctl(_, LOADPIN_IOC_SET_TRUSTED_VERITY_DIGESTS, _))
      .WillOnce(Return(0));
  EXPECT_TRUE(startup::SetupLoadPinVerityDigests(platform_.get(), base_dir_,
                                                 mock_startup_dep_.get()));
}

TEST_F(SecurityManagerLoadPinTest, FailureToReadDigests) {
  // List all the expected calls to OpenFile.
  EXPECT_CALL(*platform_, OpenFile(loadpin_verity_path_, "w"));
  EXPECT_CALL(*platform_, OpenFile(trusted_verity_digests_path_, "r"))
      .WillOnce(
          DoAll(InvokeWithoutArgs([] { errno = EACCES; }), Return(nullptr)));
  EXPECT_CALL(*platform_, OpenFile(dev_null_path_, "r"));
  EXPECT_CALL(*platform_, Ioctl(_, LOADPIN_IOC_SET_TRUSTED_VERITY_DIGESTS, _))
      .WillOnce(Return(0));

  EXPECT_TRUE(startup::SetupLoadPinVerityDigests(platform_.get(), base_dir_,
                                                 mock_startup_dep_.get()));
}

TEST_F(SecurityManagerLoadPinTest, FailureToReadInvalidDigestsDevNull) {
  EXPECT_CALL(*platform_, OpenFile(loadpin_verity_path_, "w"));
  EXPECT_CALL(*platform_, OpenFile(trusted_verity_digests_path_, "r"))
      .WillOnce(
          DoAll(InvokeWithoutArgs([] { errno = EACCES; }), Return(nullptr)));
  EXPECT_CALL(*platform_, OpenFile(dev_null_path_, "r"))
      .WillOnce(
          DoAll(InvokeWithoutArgs([] { errno = EACCES; }), Return(nullptr)));

  EXPECT_FALSE(startup::SetupLoadPinVerityDigests(platform_.get(), base_dir_,
                                                  mock_startup_dep_.get()));
}

TEST_F(SecurityManagerLoadPinTest, FailureToFeedLoadPin) {
  EXPECT_CALL(*platform_, Ioctl(_, LOADPIN_IOC_SET_TRUSTED_VERITY_DIGESTS, _))
      .WillOnce(Return(-1));

  EXPECT_FALSE(startup::SetupLoadPinVerityDigests(platform_.get(), base_dir_,
                                                  mock_startup_dep_.get()));
}

class SysKeyTest : public ::testing::Test {
 protected:
  SysKeyTest() {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir = temp_dir_.GetPath();
    stateful = base_dir.Append(kStatefulPartition);
    platform_ = std::make_unique<libstorage::MockPlatform>();
    startup_dep_ = std::make_unique<startup::FakeStartupDep>(platform_.get());
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir;
  base::FilePath stateful;
  std::unique_ptr<libstorage::MockPlatform> platform_;
  std::unique_ptr<startup::FakeStartupDep> startup_dep_;
};

TEST_F(SysKeyTest, NoEarlySysKeyFile) {
  base::FilePath no_early = stateful.Append(".no_early_system_key");
  ASSERT_TRUE(platform_->WriteStringToFile(no_early, "1"));

  std::string res;
  startup::CreateSystemKey(platform_.get(), base_dir, stateful,
                           startup_dep_.get(), &res);

  EXPECT_EQ(res, "Opt not to create a system key in advance.");
}

TEST_F(SysKeyTest, AlreadySysKey) {
  startup_dep_->SetMountEncOutputForArg("info", "NVRAM: available.");

  std::string res;
  startup::CreateSystemKey(platform_.get(), base_dir, stateful,
                           startup_dep_.get(), &res);
  std::string expected =
      "Checking if a system key already exists in NVRAM...\n";
  expected.append("NVRAM: available.\n");
  expected.append("There is already a system key in NVRAM.\n");
  EXPECT_EQ(res, expected);
}

TEST_F(SysKeyTest, NeedSysKeyBadRandomWrite) {
  base::FilePath backup = stateful.Append(kPreserveSysKeyFile);
  EXPECT_CALL(*platform_, WriteArrayToFile(backup, _, _))
      .WillOnce(Return(false));
  startup_dep_->SetMountEncOutputForArg("info", "not found.");

  std::string res;
  startup::CreateSystemKey(platform_.get(), base_dir, stateful,
                           startup_dep_.get(), &res);

  std::string expected =
      "Checking if a system key already exists in NVRAM...\n";
  expected.append("not found.\n");
  expected.append("No system key found in NVRAM. Start creating one.\n");
  expected.append("Failed to generate or back up system key material.\n");
  EXPECT_EQ(res, expected);
}

TEST_F(SysKeyTest, NeedSysKeySuccessful) {
  base::FilePath backup = stateful.Append(kPreserveSysKeyFile);
  startup_dep_->SetMountEncOutputForArg("info", "not found.");
  startup_dep_->SetMountEncOutputForArg("set", "MountEncrypted set output.\n");
  std::string res;
  startup::CreateSystemKey(platform_.get(), base_dir, stateful,
                           startup_dep_.get(), &res);

  std::string expected =
      "Checking if a system key already exists in NVRAM...\n";
  expected.append("not found.\n");
  expected.append("No system key found in NVRAM. Start creating one.\n");
  expected.append("MountEncrypted set output.\n");
  expected.append("Successfully created a system key.");
  EXPECT_EQ(res, expected);
}

class ExceptionsTest : public ::testing::Test {
 protected:
  ExceptionsTest() {}

  void SetUp() override {
    platform_ = std::make_unique<libstorage::FakePlatform>();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir = temp_dir_.GetPath();
    allow_file_ = base_dir.Append("allow_file");
    ASSERT_TRUE(platform_->WriteStringToFile(allow_file_, ""));
    excepts_dir_ = base_dir.Append("excepts_dir");
  }

  std::unique_ptr<libstorage::FakePlatform> platform_;
  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir;
  base::FilePath allow_file_;
  base::FilePath excepts_dir_;
};

TEST_F(ExceptionsTest, ExceptionsDirNoExist) {
  startup::ExceptionsProjectSpecific(platform_.get(), base_dir, excepts_dir_,
                                     &ExceptionsTestFunc);
  std::string allow_contents;
  platform_->ReadFileToString(allow_file_, &allow_contents);
  EXPECT_EQ(allow_contents, "");
}

TEST_F(ExceptionsTest, ExceptionsDirEmpty) {
  platform_->CreateDirectory(excepts_dir_);
  startup::ExceptionsProjectSpecific(platform_.get(), base_dir, excepts_dir_,
                                     &ExceptionsTestFunc);
  std::string allow_contents;
  platform_->ReadFileToString(allow_file_, &allow_contents);
  EXPECT_EQ(allow_contents, "");
}

TEST_F(ExceptionsTest, ExceptionsDirMultiplePaths) {
  base::FilePath test_path_1_1 = base_dir.Append("test_1_1");
  base::FilePath test_path_1_2 = base_dir.Append("test_1_2");
  base::FilePath test_path_1_ignore = base_dir.Append("should_ignore");
  std::string test_str_1 = std::string("\n")
                               .append(test_path_1_1.value())
                               .append("\n#ignore\n\n#")
                               .append(test_path_1_ignore.value())
                               .append("\n")
                               .append(test_path_1_2.value())
                               .append("\n");
  base::FilePath test_path_2_1 = base_dir.Append("test_2_1");
  base::FilePath test_path_2_2 = base_dir.Append("test_2_2");
  base::FilePath test_path_2_ignore = base_dir.Append("should_ignore");
  std::string test_str_2 = std::string("#")
                               .append(test_path_2_ignore.value())
                               .append("\n")
                               .append(test_path_2_1.value())
                               .append("\n\n#\n")
                               .append(test_path_2_2.value());
  base::FilePath test_1 = excepts_dir_.Append("test_1");
  base::FilePath test_2 = excepts_dir_.Append("test_2");
  ASSERT_TRUE(platform_->WriteStringToFile(test_1, test_str_1));
  ASSERT_TRUE(platform_->WriteStringToFile(test_2, test_str_2));

  startup::ExceptionsProjectSpecific(platform_.get(), base_dir, excepts_dir_,
                                     &ExceptionsTestFunc);

  std::string allow_contents;
  platform_->ReadFileToString(allow_file_, &allow_contents);
  EXPECT_NE(allow_contents.find(test_path_1_1.value()), std::string::npos);
  EXPECT_NE(allow_contents.find(test_path_1_2.value()), std::string::npos);
  EXPECT_EQ(allow_contents.find(test_path_1_ignore.value()), std::string::npos);
  EXPECT_NE(allow_contents.find(test_path_2_1.value()), std::string::npos);
  EXPECT_NE(allow_contents.find(test_path_2_2.value()), std::string::npos);
  EXPECT_EQ(allow_contents.find(test_path_1_ignore.value()), std::string::npos);
  EXPECT_TRUE(platform_->DirectoryExists(test_path_1_1));
  EXPECT_TRUE(platform_->DirectoryExists(test_path_1_2));
  EXPECT_FALSE(platform_->DirectoryExists(test_path_1_ignore));
  EXPECT_TRUE(platform_->DirectoryExists(test_path_2_1));
  EXPECT_TRUE(platform_->DirectoryExists(test_path_2_2));
  EXPECT_FALSE(platform_->DirectoryExists(test_path_2_ignore));
}
