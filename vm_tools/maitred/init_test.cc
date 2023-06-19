// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/types.h>

#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <gtest/gtest.h>

#include "vm_tools/maitred/init.h"

namespace vm_tools {
namespace maitred {

TEST(ImplTest, ParseHostnameParsesTypicalCase) {
  std::string etc_hostname("Chromebook\n");
  EXPECT_EQ(ParseHostname(etc_hostname), "Chromebook");
}

TEST(ImplTest, ParseHostnameIgnoresComments) {
  std::string etc_hostname("# this is a comment\nChromebook\n");
  EXPECT_EQ(ParseHostname(etc_hostname), "Chromebook");
}

TEST(ImplTest, ParseHostnameHandlesEmptyCase) {
  std::string etc_hostname;
  EXPECT_EQ(ParseHostname(etc_hostname), "");
}

TEST(ImplTest, ParseHostnameIgnoresMultipleNames) {
  std::string etc_hostname("one\ntwo\n");
  EXPECT_EQ(ParseHostname(etc_hostname), "one");
}

std::string ReadCmdline(const base::FilePath& path);
TEST(ImplTest, ReadCmdLine) {
  using std::string_literals::operator""s;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  auto path = dir.GetPath().Append("cmdline");
  std::string data = "foo\0--bar"s;
  LOG(ERROR) << data.size();
  ASSERT_TRUE(base::WriteFile(path, data));

  auto s = ReadCmdline(path);
  EXPECT_EQ(s, "foo --bar");
}

ino_t GetInode(const base::FilePath& path);

TEST(ImplTest, GetInode) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  auto root = dir.GetPath();
  auto link = dir.GetPath().Append("link");
  auto other = dir.GetPath().Append("other");
  base::CreateSymbolicLink(root, link);
  base::CreateDirectory(other);

  // Not going to reimplement `GetInode` to check what the actual inode is, just
  // assume that if different files have different inodes, inodes are non-zero,
  // and a link has the same inode as a file that it's good.
  ASSERT_NE(GetInode(root), 0);
  ASSERT_EQ(GetInode(root), GetInode(link));
  ASSERT_NE(GetInode(root), GetInode(other));
}

std::string SanitiseCmdline(const std::string& cmdline,
                            ino_t root_inode,
                            ino_t proc_inode);

TEST(ImplTest, SanitiseCmdlineUnknown) {
  ASSERT_EQ(SanitiseCmdline("unknown-process", 0, 1), "container process");
}

TEST(ImplTest, SanitiseCmdlineEmptyString) {
  ASSERT_EQ(SanitiseCmdline("", 0, 0), "unknown process");
}

TEST(ImplTest, SanitiseCmdlineNonNamespaced) {
  ASSERT_EQ(SanitiseCmdline("cmdline goes here", 1234, 1234),
            "cmdline goes here");
}

TEST(ImplTest, SanitiseCmdlineOptGoogle) {
  std::string cmdline =
      "/opt/google/cros-containers/bin/../lib/ld-linux-x86-64.so.2 --argv0 "
      "/usr/bin/sommelier --library-path "
      "/opt/google/cros-containers/bin/../lib --inhibit-rpath ...";
  std::string expected =
      "/opt/google/cros-containers/bin/../lib/ld-linux-x86-64.so.2 --argv0 "
      "/usr/bin/sommelier";
  ASSERT_EQ(SanitiseCmdline(cmdline, 1, 2), expected);
}

TEST(ImplTest, SanitiseCmdlineInvalidOptGoogle) {
  // If we get a truncated cmdline somehow, it should be returned as-is.
  std::string cmdline =
      "/opt/google/cros-containers/bin/../lib/ld-linux-x86-64.so.2 --argv0";
  ASSERT_EQ(SanitiseCmdline(cmdline, 1, 2), cmdline);
  ASSERT_EQ(SanitiseCmdline(cmdline + " ", 1, 2), cmdline + " ");
}
}  // namespace maitred
}  // namespace vm_tools
