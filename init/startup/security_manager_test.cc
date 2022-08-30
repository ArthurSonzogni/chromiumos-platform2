// Copyright 2022 The ChromiumOS Authors.
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
#include <gtest/gtest.h>

#include "init/startup/mock_platform_impl.h"
#include "init/startup/security_manager.h"

using testing::_;
using testing::ByMove;
using testing::DoAll;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::StrictMock;

namespace {

// Define in test to catch source changes in flags.
constexpr auto kWriteFlags = O_WRONLY | O_NOFOLLOW | O_CLOEXEC;
constexpr auto kReadFlags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC;

MATCHER_P(IntPtrCheck, expected, "") {
  return *arg == expected;
}

// Helper function to create directory and write to file.
bool CreateDirAndWriteFile(const base::FilePath& path,
                           const std::string& contents) {
  return base::CreateDirectory(path.DirName()) &&
         base::WriteFile(path, contents.c_str(), contents.length()) ==
             contents.length();
}

}  // namespace

class SecurityManagerTest : public ::testing::Test {
 protected:
  SecurityManagerTest() {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir_ = temp_dir_.GetPath();
    mock_platform_ = std::make_unique<StrictMock<startup::MockPlatform>>();
  }

  std::unique_ptr<startup::MockPlatform> mock_platform_;

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
    CreateDirAndWriteFile(loadpin_verity_path_, kNull);

    trusted_verity_digests_path_ =
        base_dir_.Append("opt/google/dlc/_trusted_verity_digests");
    CreateDirAndWriteFile(trusted_verity_digests_path_, kRootDigest);

    dev_null_path_ = base_dir_.Append("dev/null");
    CreateDirAndWriteFile(dev_null_path_, kNull);
  }

  base::FilePath loadpin_verity_path_;
  base::FilePath trusted_verity_digests_path_;
  base::FilePath dev_null_path_;
  const std::string kRootDigest =
      "fb066d299c657b127ecc2c11f841cabf14c717eb6f03630ef788e6e1cca17f52";
  const std::string kNull = "\0";
};

TEST_F(SecurityManagerTest, Before_v4_4) {
  base::FilePath policies_dir =
      base_dir_.Append("usr/share/cros/startup/process_management_policies");
  base::FilePath mgmt_policies = base_dir_.Append(
      "sys/kernel/security/chromiumos/process_management_policies/"
      "add_whitelist_policy");
  ASSERT_TRUE(CreateDirAndWriteFile(mgmt_policies, ""));
  base::FilePath safesetid_mgmt_policies =
      base_dir_.Append("sys/kernel/security/safesetid/whitelist_policy");
  ASSERT_TRUE(CreateDirAndWriteFile(safesetid_mgmt_policies, "#AllowList"));
  base::FilePath allow_1 = policies_dir.Append("allow_1.txt");
  ASSERT_TRUE(CreateDirAndWriteFile(allow_1, "254:607\n607:607"));

  startup::ConfigureProcessMgmtSecurity(base_dir_);

  std::string allow;
  base::ReadFileToString(mgmt_policies, &allow);
  EXPECT_EQ(allow, "254:607\n607:607\n");
}

TEST_F(SecurityManagerTest, After_v4_14) {
  base::FilePath policies_dir =
      base_dir_.Append("usr/share/cros/startup/process_management_policies");
  base::FilePath mgmt_policies =
      base_dir_.Append("sys/kernel/security/safesetid/whitelist_policy");
  ASSERT_TRUE(CreateDirAndWriteFile(mgmt_policies, "#AllowList"));
  base::FilePath allow_1 = policies_dir.Append("allow_1.txt");
  std::string result1 = "254:607\n607:607";
  std::string full1 = "254:607\n607:607\n#Comment\n\n#Ignore";
  ASSERT_TRUE(CreateDirAndWriteFile(allow_1, full1));
  base::FilePath allow_2 = policies_dir.Append("allow_2.txt");
  std::string result2 = "20104:224\n20104:217\n217:217";
  std::string full2 = "#Comment\n\n20104:224\n20104:217\n#Ignore\n217:217";
  ASSERT_TRUE(CreateDirAndWriteFile(allow_2, full2));

  startup::ConfigureProcessMgmtSecurity(base_dir_);

  std::string allow;
  base::ReadFileToString(mgmt_policies, &allow);

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
  ASSERT_TRUE(CreateDirAndWriteFile(mgmt_policies, "#AllowList"));
  base::FilePath allow_1 = policies_dir.Append("allow_1.txt");
  std::string result1 = "254:607\n607:607";
  ASSERT_TRUE(CreateDirAndWriteFile(allow_1, result1));
  base::FilePath allow_2 = policies_dir.Append("allow_2.txt");
  std::string result2 = "20104:224\n20104:217\n217:217";
  ASSERT_TRUE(CreateDirAndWriteFile(allow_2, result2));

  startup::ConfigureProcessMgmtSecurity(base_dir_);

  std::string allow;
  base::ReadFileToString(mgmt_policies, &allow);

  EXPECT_NE(allow.find(result1), std::string::npos);
  EXPECT_NE(allow.find(result2), std::string::npos);
}

TEST_F(SecurityManagerTest, EmptyAfter_v5_9) {
  base::FilePath mgmt_policies =
      base_dir_.Append("sys/kernel/security/safesetid/uid_allowlist_policy");
  ASSERT_TRUE(CreateDirAndWriteFile(mgmt_policies, "#AllowList"));

  EXPECT_EQ(startup::ConfigureProcessMgmtSecurity(base_dir_), false);

  std::string allow;
  base::ReadFileToString(mgmt_policies, &allow);

  EXPECT_EQ(allow, "#AllowList");
}

TEST_F(SecurityManagerLoadPinTest, LoadPinAttributeUnsupported) {
  ASSERT_TRUE(base::DeleteFile(loadpin_verity_path_));

  base::ScopedFD loadpin_verity(
      HANDLE_EINTR(open(loadpin_verity_path_.value().c_str(),
                        O_RDONLY | O_NOFOLLOW | O_CLOEXEC)));
  // int fd = loadpin_verity.get();
  EXPECT_CALL(*mock_platform_, Open(loadpin_verity_path_, _))
      .WillOnce(Return(ByMove(std::move(loadpin_verity))));
  // `loadpin_verity` is moved, do not use.
  EXPECT_CALL(*mock_platform_, Ioctl(_, _, _)).Times(0);

  EXPECT_TRUE(
      startup::SetupLoadPinVerityDigests(base_dir_, mock_platform_.get()));
}

TEST_F(SecurityManagerLoadPinTest, FailureToOpenLoadPinVerity) {
  ASSERT_TRUE(base::DeleteFile(loadpin_verity_path_));

  base::ScopedFD loadpin_verity(
      HANDLE_EINTR(open(loadpin_verity_path_.value().c_str(), kWriteFlags)));
  // int fd = loadpin_verity.get();
  EXPECT_CALL(*mock_platform_, Open(loadpin_verity_path_, kWriteFlags))
      .WillOnce(DoAll(
          // Override the `errno` to be non-`ENOENT`.
          InvokeWithoutArgs([] { errno = EACCES; }),
          Return(ByMove(std::move(loadpin_verity)))));
  // `loadpin_verity` is moved, do not use.
  EXPECT_CALL(*mock_platform_, Ioctl(_, _, _)).Times(0);

  // The call should fail as failure to open LoadPin verity file.
  EXPECT_FALSE(
      startup::SetupLoadPinVerityDigests(base_dir_, mock_platform_.get()));
}

TEST_F(SecurityManagerLoadPinTest, ValidDigests) {
  base::ScopedFD loadpin_verity(
      HANDLE_EINTR(open(loadpin_verity_path_.value().c_str(), kWriteFlags)));
  int fd = loadpin_verity.get();

  base::ScopedFD trusted_verity_digests(HANDLE_EINTR(
      open(trusted_verity_digests_path_.value().c_str(), kReadFlags)));
  int digests_fd = trusted_verity_digests.get();

  EXPECT_CALL(*mock_platform_, Open(loadpin_verity_path_, kWriteFlags))
      .WillOnce(Return(ByMove(std::move(loadpin_verity))));
  // `loadpin_verity` is moved, do not use.

  EXPECT_CALL(*mock_platform_, Open(trusted_verity_digests_path_, kReadFlags))
      .WillOnce(Return(ByMove(std::move(trusted_verity_digests))));
  // `trusted_verity_digests` is moved, do not use.

  EXPECT_CALL(*mock_platform_, Ioctl(fd, _, IntPtrCheck(digests_fd)))
      .WillOnce(Return(0));

  EXPECT_TRUE(
      startup::SetupLoadPinVerityDigests(base_dir_, mock_platform_.get()));
}

TEST_F(SecurityManagerLoadPinTest, MissingDigests) {
  ASSERT_TRUE(base::DeleteFile(trusted_verity_digests_path_));

  base::ScopedFD loadpin_verity(
      HANDLE_EINTR(open(loadpin_verity_path_.value().c_str(), kWriteFlags)));
  int fd = loadpin_verity.get();

  base::ScopedFD trusted_verity_digests(HANDLE_EINTR(
      open(trusted_verity_digests_path_.value().c_str(), kReadFlags)));

  base::ScopedFD dev_null(
      HANDLE_EINTR(open(dev_null_path_.value().c_str(), kReadFlags)));
  int dev_null_fd = dev_null.get();

  EXPECT_CALL(*mock_platform_, Open(loadpin_verity_path_, kWriteFlags))
      .WillOnce(Return(ByMove(std::move(loadpin_verity))));
  // `loadpin_verity` is moved, do not use.

  EXPECT_CALL(*mock_platform_, Open(trusted_verity_digests_path_, kReadFlags))
      .WillOnce(Return(ByMove(std::move(trusted_verity_digests))));
  // `trusted_verity_digests` is moved, do not use.

  EXPECT_CALL(*mock_platform_, Open(dev_null_path_, kReadFlags))
      .WillOnce(Return(ByMove(std::move(dev_null))));
  // `dev_null` is moved, do not use.

  EXPECT_CALL(*mock_platform_, Ioctl(fd, _, IntPtrCheck(dev_null_fd)))
      .WillOnce(Return(0));

  EXPECT_TRUE(
      startup::SetupLoadPinVerityDigests(base_dir_, mock_platform_.get()));
}

TEST_F(SecurityManagerLoadPinTest, FailureToReadDigests) {
  ASSERT_TRUE(base::DeleteFile(trusted_verity_digests_path_));

  base::ScopedFD loadpin_verity(
      HANDLE_EINTR(open(loadpin_verity_path_.value().c_str(), kWriteFlags)));
  int fd = loadpin_verity.get();

  base::ScopedFD trusted_verity_digests(HANDLE_EINTR(
      open(trusted_verity_digests_path_.value().c_str(), kReadFlags)));

  base::ScopedFD dev_null(
      HANDLE_EINTR(open(dev_null_path_.value().c_str(), kReadFlags)));
  int dev_null_fd = dev_null.get();

  EXPECT_CALL(*mock_platform_, Open(loadpin_verity_path_, kWriteFlags))
      .WillOnce(Return(ByMove(std::move(loadpin_verity))));
  // `loadpin_verity` is moved, do not use.

  EXPECT_CALL(*mock_platform_, Open(trusted_verity_digests_path_, kReadFlags))
      .WillOnce(DoAll(
          // Override the `errno` to be non-`ENOENT`.
          InvokeWithoutArgs([] { errno = EACCES; }),
          Return(ByMove(std::move(trusted_verity_digests)))));
  // `trusted_verity_digests` is moved, do not use.

  EXPECT_CALL(*mock_platform_, Open(dev_null_path_, kReadFlags))
      .WillOnce(Return(ByMove(std::move(dev_null))));
  // `dev_null` is moved, do not use.

  EXPECT_CALL(*mock_platform_, Ioctl(fd, _, IntPtrCheck(dev_null_fd)))
      .WillOnce(Return(0));

  EXPECT_TRUE(
      startup::SetupLoadPinVerityDigests(base_dir_, mock_platform_.get()));
}

TEST_F(SecurityManagerLoadPinTest, FailureToReadInvalidDigestsDevNull) {
  ASSERT_TRUE(base::DeleteFile(trusted_verity_digests_path_));
  ASSERT_TRUE(base::DeleteFile(dev_null_path_));

  base::ScopedFD loadpin_verity(
      HANDLE_EINTR(open(loadpin_verity_path_.value().c_str(), kWriteFlags)));

  base::ScopedFD trusted_verity_digests(HANDLE_EINTR(
      open(trusted_verity_digests_path_.value().c_str(), kReadFlags)));

  base::ScopedFD dev_null(
      HANDLE_EINTR(open(dev_null_path_.value().c_str(), kReadFlags)));

  EXPECT_CALL(*mock_platform_, Open(loadpin_verity_path_, kWriteFlags))
      .WillOnce(Return(ByMove(std::move(loadpin_verity))));
  // `loadpin_verity` is moved, do not use.

  EXPECT_CALL(*mock_platform_, Open(trusted_verity_digests_path_, kReadFlags))
      .WillOnce(Return(ByMove(std::move(trusted_verity_digests))));
  // `trusted_verity_digests` is moved, do not use.

  EXPECT_CALL(*mock_platform_, Open(dev_null_path_, kReadFlags))
      .WillOnce(Return(ByMove(std::move(dev_null))));
  // `dev_null` is moved, do not use.

  EXPECT_CALL(*mock_platform_, Ioctl(_, _, _)).Times(0);

  EXPECT_FALSE(
      startup::SetupLoadPinVerityDigests(base_dir_, mock_platform_.get()));
}

TEST_F(SecurityManagerLoadPinTest, FailureToFeedLoadPin) {
  base::ScopedFD loadpin_verity(
      HANDLE_EINTR(open(loadpin_verity_path_.value().c_str(), kWriteFlags)));
  int fd = loadpin_verity.get();

  base::ScopedFD trusted_verity_digests(HANDLE_EINTR(
      open(trusted_verity_digests_path_.value().c_str(), kReadFlags)));
  int digests_fd = trusted_verity_digests.get();

  EXPECT_CALL(*mock_platform_, Open(loadpin_verity_path_, kWriteFlags))
      .WillOnce(Return(ByMove(std::move(loadpin_verity))));
  // `loadpin_verity` is moved, do not use.

  EXPECT_CALL(*mock_platform_, Open(trusted_verity_digests_path_, kReadFlags))
      .WillOnce(Return(ByMove(std::move(trusted_verity_digests))));
  // `trusted_verity_digests` is moved, do not use.

  EXPECT_CALL(*mock_platform_, Ioctl(fd, _, IntPtrCheck(digests_fd)))
      .WillOnce(Return(-1));

  EXPECT_FALSE(
      startup::SetupLoadPinVerityDigests(base_dir_, mock_platform_.get()));
}
