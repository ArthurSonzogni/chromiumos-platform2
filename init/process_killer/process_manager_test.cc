// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/process_killer/process_manager.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <init/process_killer/process.h>
#include <re2/re2.h>

namespace init {
namespace {

std::optional<ActiveProcess> GetProcessFromPid(
    const std::vector<ActiveProcess>& list, pid_t pid) {
  for (const ActiveProcess& p : list) {
    if (p.GetPid() == pid) {
      return p;
    }
  }
  return std::nullopt;
}

}  // namespace

class ProcessManagerTest : public ::testing::Test {
 public:
  void SetUpProcess(pid_t pid,
                    const std::string& mnt_ns,
                    const std::string& comm,
                    const std::string& mountinfo_contents,
                    const std::vector<OpenFileDescriptor>& fds,
                    const std::vector<OpenFileDescriptor>& maps) {
    base::FilePath pid_dir =
        tmp_dir_.GetPath().Append(base::NumberToString(pid));
    ASSERT_TRUE(base::CreateDirectory(pid_dir));

    // Set up mountinfo.
    base::FilePath mountinfo = pid_dir.Append("mountinfo");
    ASSERT_TRUE(base::WriteFile(mountinfo, mountinfo_contents));

    // Set up comm for the process.
    base::FilePath comm_path = pid_dir.Append("comm");
    ASSERT_TRUE(base::WriteFile(comm_path, comm));

    // Set up file descriptors.
    base::FilePath fd_dir = pid_dir.Append("fd");
    ASSERT_TRUE(base::CreateDirectory(fd_dir));

    // Set up file descriptors. Add a few fake descriptors at the beginning to
    // test handling of stdin/stdout/pipes.
    ASSERT_TRUE(base::WriteFile(fd_dir.Append("0"), "foo"));
    ASSERT_TRUE(base::WriteFile(fd_dir.Append("1"), "bar"));
    ASSERT_TRUE(base::WriteFile(fd_dir.Append("2"), "baz"));

    // Set up map_files.
    base::FilePath map_files = pid_dir.Append("map_files");
    ASSERT_TRUE(base::WriteFile(map_files, "foo"));

    int current_fd = 3;
    // Target dir contains the targets for the symbolic links.
    base::FilePath target_dir = tmp_dir_.GetPath().Append("targets");
    ASSERT_TRUE(base::CreateDirectory(target_dir));

    for (auto& fd : fds) {
      base::FilePath symlink =
          fd_dir.Append(base::NumberToString(current_fd++));
      base::FilePath target = target_dir.Append(fd.path.value());
      ASSERT_TRUE(base::WriteFile(target, "foo"));
      ASSERT_TRUE(base::CreateSymbolicLink(target, symlink));
    }

    for (auto& map : maps) {
      base::FilePath symlink = fd_dir.Append(map.path.value());
      base::FilePath target = target_dir.Append(map.path.value());
      ASSERT_TRUE(base::WriteFile(target, "foo"));
      ASSERT_TRUE(base::CreateSymbolicLink(target, symlink));
    }

    // Set up mount namespace symlink. Re-use the fd directory for storing
    // the target.
    base::FilePath ns_dir = pid_dir.Append("ns");
    ASSERT_TRUE(base::CreateDirectory(ns_dir));

    base::FilePath mnt_ns_path = target_dir.Append(mnt_ns);
    ASSERT_TRUE(base::WriteFile(mnt_ns_path, "foo"));
    ASSERT_TRUE(base::CreateSymbolicLink(mnt_ns_path, ns_dir.Append("mnt")));
  }

  void SetUp() override {
    ASSERT_TRUE(tmp_dir_.CreateUniqueTempDir());
    pm_ = std::make_unique<ProcessManager>(tmp_dir_.GetPath());

    // Set up init process.
    std::string mountinfo =
        "21 12 8:1 /var /var rw,noexec - ext3 /dev/sda1 rw\n";
    OpenFileDescriptor fd = {base::FilePath("foo")};
    OpenFileDescriptor maps = {base::FilePath("abcd")};
    SetUpProcess(1, "init_mnt_ns", "/sbin/init", mountinfo, {fd}, {maps});
  }

 protected:
  base::ScopedTempDir tmp_dir_;
  std::unique_ptr<ProcessManager> pm_;
};

TEST_F(ProcessManagerTest, InvalidProcessTest) {
  base::FilePath proc_dir = tmp_dir_.GetPath().Append("proc");
  ASSERT_TRUE(base::CreateDirectory(proc_dir));
  ASSERT_TRUE(base::WriteFile(proc_dir.Append("123"), "foo"));
  EXPECT_EQ(pm_->GetProcessList(true, true).size(), 1);
}

TEST_F(ProcessManagerTest, ValidProcessTest) {
  std::string mountinfo = "21 12 8:1 /var /var rw,noexec - ext3 /dev/sda1 rw\n";

  OpenFileDescriptor fd = {base::FilePath("foo")};
  OpenFileDescriptor maps = {base::FilePath("abcd")};
  SetUpProcess(2, "init_mnt_ns", "foo", mountinfo, {fd}, {maps});

  std::vector<ActiveProcess> list = pm_->GetProcessList(true, true);
  EXPECT_EQ(list.size(), 2);
  auto p = GetProcessFromPid(list, 2);
  EXPECT_TRUE(p);

  EXPECT_TRUE(p->HasFileOpenOnMount(re2::RE2("foo")));
  EXPECT_TRUE(p->HasFileOpenOnMount(re2::RE2("abcd")));
  EXPECT_TRUE(p->HasMountOpenFromDevice(re2::RE2("/dev/sda1")));
  EXPECT_TRUE(p->InInitMountNamespace());
}

TEST_F(ProcessManagerTest, ValidNamespacedProcessTest) {
  std::string mountinfo = "21 12 8:1 /var /var rw,noexec - ext3 /dev/sda1 rw\n";

  OpenFileDescriptor fd = {base::FilePath("foo")};
  OpenFileDescriptor maps = {base::FilePath("abcd")};
  SetUpProcess(2, "separate_mnt_ns", "foo", mountinfo, {fd}, {maps});

  std::vector<ActiveProcess> list = pm_->GetProcessList(true, true);
  EXPECT_EQ(list.size(), 2);
  auto p = GetProcessFromPid(list, 2);
  EXPECT_TRUE(p);

  EXPECT_TRUE(p->HasFileOpenOnMount(re2::RE2("foo")));
  EXPECT_TRUE(p->HasFileOpenOnMount(re2::RE2("abcd")));
  EXPECT_TRUE(p->HasMountOpenFromDevice(re2::RE2("/dev/sda1")));
  EXPECT_FALSE(p->InInitMountNamespace());
}

}  // namespace init
