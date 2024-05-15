// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/userdb_utils.h>

#include <gtest/gtest.h>

namespace brillo {

const char kPasswdContent[] = R"""(
chronos:x:1000:1000:system_user:/home/chronos/user:/bin/bash
root:x:0:0:root:/root:/bin/bash
bin:!:1:1:bin:/bin:/bin/false
daemon:!:2:2:daemon:/sbin:/bin/false
)""";

const char kGroupContent[] = R"""(
dns-proxy:!:20167:dns-proxy
debugd:!:216:debugd
debugd-logs:!:235:debugd-logs
daemon-store:!:400:biod,chaps,crosvm,shill
)""";

TEST(UserdbUtils, GetUsers) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath file_path = temp_dir.GetPath().Append("passwd");
  ASSERT_TRUE(base::WriteFile(file_path, kPasswdContent));
  std::vector<uid_t> users = userdb::GetUsers(file_path);
  std::vector<uid_t> expected = {1000, 0, 1, 2};
  EXPECT_EQ(users, expected);
}

TEST(UserdbUtils, GetGroups) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath file_path = temp_dir.GetPath().Append("group");
  ASSERT_TRUE(base::WriteFile(file_path, kGroupContent));
  std::vector<uid_t> groups = userdb::GetGroups(file_path);
  std::vector<uid_t> expected = {20167, 216, 235, 400};
  EXPECT_EQ(groups, expected);
}

}  // namespace brillo
