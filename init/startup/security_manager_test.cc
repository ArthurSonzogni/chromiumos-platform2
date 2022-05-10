// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#include <memory>
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

#include "init/startup/security_manager.h"

namespace {

// Helper function to create directory and write to file.
bool CreateDirAndWriteFile(const base::FilePath& path,
                           const std::string& contents) {
  return base::CreateDirectory(path.DirName()) &&
         base::WriteFile(path, contents.c_str(), contents.length()) ==
             contents.length();
}

}  // namespace

class ConfigProcessManagementTest : public ::testing::Test {
 protected:
  ConfigProcessManagementTest() {}

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    base_dir = temp_dir_.GetPath();
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath base_dir;
};

TEST_F(ConfigProcessManagementTest, Before_v4_4) {
  base::FilePath policies_dir =
      base_dir.Append("usr/share/cros/startup/process_management_policies");
  base::FilePath mgmt_policies = base_dir.Append(
      "sys/kernel/security/chromiumos/process_management_policies/"
      "add_whitelist_policy");
  ASSERT_TRUE(CreateDirAndWriteFile(mgmt_policies, ""));
  base::FilePath safesetid_mgmt_policies =
      base_dir.Append("sys/kernel/security/safesetid/whitelist_policy");
  ASSERT_TRUE(CreateDirAndWriteFile(safesetid_mgmt_policies, "#AllowList"));
  base::FilePath allow_1 = policies_dir.Append("allow_1.txt");
  ASSERT_TRUE(CreateDirAndWriteFile(allow_1, "254:607\n607:607"));

  startup::ConfigureProcessMgmtSecurity(base_dir);

  std::string allow;
  base::ReadFileToString(mgmt_policies, &allow);
  EXPECT_EQ(allow, "254:607\n607:607");
}

TEST_F(ConfigProcessManagementTest, After_v4_14) {
  base::FilePath policies_dir =
      base_dir.Append("usr/share/cros/startup/process_management_policies");
  base::FilePath mgmt_policies =
      base_dir.Append("sys/kernel/security/safesetid/whitelist_policy");
  ASSERT_TRUE(CreateDirAndWriteFile(mgmt_policies, "#AllowList"));
  base::FilePath allow_1 = policies_dir.Append("allow_1.txt");
  std::string result1 = "254:607\n607:607";
  std::string full1 = "254:607\n607:607#Comment\n\n#Ignore";
  ASSERT_TRUE(CreateDirAndWriteFile(allow_1, full1));
  base::FilePath allow_2 = policies_dir.Append("allow_2.txt");
  std::string result2 = "20104:224\n20104:217\n217:217";
  std::string full2 = "#Comment\n\n20104:224\n20104:217\n#Ignore\n217:217";
  ASSERT_TRUE(CreateDirAndWriteFile(allow_2, full2));

  startup::ConfigureProcessMgmtSecurity(base_dir);

  std::string allow;
  base::ReadFileToString(mgmt_policies, &allow);

  EXPECT_NE(allow.find(result1), std::string::npos);
  EXPECT_NE(allow.find(result2), std::string::npos);
}

TEST_F(ConfigProcessManagementTest, After_v5_9) {
  base::FilePath policies_dir =
      base_dir.Append("usr/share/cros/startup/process_management_policies");
  base::FilePath mgmt_policies =
      base_dir.Append("sys/kernel/security/safesetid/uid_allowlist_policy");
  ASSERT_TRUE(CreateDirAndWriteFile(mgmt_policies, "#AllowList"));
  base::FilePath allow_1 = policies_dir.Append("allow_1.txt");
  std::string result1 = "254:607\n607:607";
  ASSERT_TRUE(CreateDirAndWriteFile(allow_1, result1));
  base::FilePath allow_2 = policies_dir.Append("allow_2.txt");
  std::string result2 = "20104:224\n20104:217\n217:217";
  ASSERT_TRUE(CreateDirAndWriteFile(allow_2, result2));

  startup::ConfigureProcessMgmtSecurity(base_dir);

  std::string allow;
  base::ReadFileToString(mgmt_policies, &allow);

  EXPECT_NE(allow.find(result1), std::string::npos);
  EXPECT_NE(allow.find(result2), std::string::npos);
}

TEST_F(ConfigProcessManagementTest, EmptyAfter_v5_9) {
  base::FilePath mgmt_policies =
      base_dir.Append("sys/kernel/security/safesetid/uid_allowlist_policy");
  ASSERT_TRUE(CreateDirAndWriteFile(mgmt_policies, "#AllowList"));

  EXPECT_EQ(startup::ConfigureProcessMgmtSecurity(base_dir), false);

  std::string allow;
  base::ReadFileToString(mgmt_policies, &allow);

  EXPECT_EQ(allow, "#AllowList");
}
