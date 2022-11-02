// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/message_sender.h"

#include "base/files/file_path.h"
#include "base/files/file_path_watcher.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/memory/scoped_refptr.h"
#include "base/test/task_environment.h"
#include "gtest/gtest.h"
#include "missive/proto/security_xdr_events.pb.h"

namespace secagentd::testing {

namespace pb = cros_xdr::reporting;

class MessageSenderTestFixture : public ::testing::Test {
 protected:
  MessageSenderTestFixture()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
  void SetUp() override {
    ASSERT_TRUE(fake_root_.CreateUniqueTempDir());
    const base::FilePath timezone_dir =
        fake_root_.GetPath().Append("var/lib/timezone");
    ASSERT_TRUE(base::CreateDirectory(timezone_dir));
    timezone_symlink_ = timezone_dir.Append("localtime");
    zoneinfo_dir_ = fake_root_.GetPath().Append("usr/share/zoneinfo");
    ASSERT_TRUE(base::CreateDirectory(zoneinfo_dir_));

    message_sender_ = MessageSender::CreateForTesting(fake_root_.GetPath());
  }

  pb::CommonEventDataFields* GetCommon() { return &message_sender_->common_; }
  void CallInitializeDeviceBtime() { message_sender_->InitializeDeviceBtime(); }
  void CallUpdateDeviceTz() {
    message_sender_->UpdateDeviceTz(timezone_symlink_, false);
  }

  base::test::TaskEnvironment task_environment_;
  base::ScopedTempDir fake_root_;
  scoped_refptr<MessageSender> message_sender_;
  base::FilePath timezone_symlink_;
  base::FilePath zoneinfo_dir_;
};

TEST_F(MessageSenderTestFixture, TestInitializeBtime) {
  const std::string kStatContents =
      "cpu  331574 58430 92503 1962802 6568 24763 7752 0 0 0\n"
      "cpu0 18478 11108 17247 350739 777 8197 4561 0 0 0\n"
      "cpu1 22345 8002 13230 364796 1006 3470 961 0 0 0\n"
      "cpu2 23079 8248 12590 365637 1163 2955 737 0 0 0\n"
      "cpu3 23019 8297 12074 366703 1085 2756 630 0 0 0\n"
      "cpu4 108517 11661 18315 272063 1037 3519 442 0 0 0\n"
      "cpu5 136133 11112 19045 242863 1498 3863 419 0 0 0\n"
      "intr 17153789 0 1877556 2940893 0 0 22514 424451 0 0 0 0 0 0 0 0 0 0 0 "
      "0 0 0 0 0 9546173 0 756967 263 1557 1 0 0 0 288285 62 0 158 0 0 12282 "
      "128 56 82 44 15 22533 0 192916 1 17569 519 6 0 0 0 0 0 0 0 221447 0 977 "
      "0 0 0 0 10765 0 0 0 214680 14 263403 0 0 0 0 0 1 1 0 0 0 284203 14 2 1 "
      "51429 0 2 0 0 0 0 1819\n"
      "ctxt 15507989\n"
      "btime 1667427768\n"
      "processes 20013\n"
      "procs_running 1\n"
      "procs_blocked 0\n"
      "softirq 5429921 130273 509093 53702 235430 109885 0 433061 1603480 2368 "
      "2352629";
  const base::FilePath proc_dir = fake_root_.GetPath().Append("proc");
  ASSERT_TRUE(base::CreateDirectory(proc_dir));
  ASSERT_TRUE(base::WriteFile(proc_dir.Append("stat"), kStatContents));
  CallInitializeDeviceBtime();
  EXPECT_EQ(1667427768, GetCommon()->device_boot_time());
}

TEST_F(MessageSenderTestFixture, TestTzUpdateWithPrefix) {
  const base::FilePath us_dir = zoneinfo_dir_.Append("US");
  ASSERT_TRUE(base::CreateDirectory(us_dir));
  const base::FilePath pacific = us_dir.Append("Pacific");
  ASSERT_TRUE(base::WriteFile(pacific, ""));

  ASSERT_TRUE(base::CreateSymbolicLink(pacific, timezone_symlink_));
  CallUpdateDeviceTz();
  EXPECT_EQ("US/Pacific", GetCommon()->local_timezone());
}

TEST_F(MessageSenderTestFixture, TestTzUpdateWithoutPrefix) {
  // Zulu doesn't have a prefix. Probably will never happen but supported
  // nonetheless.
  const base::FilePath zulu = zoneinfo_dir_.Append("Zulu");
  ASSERT_TRUE(base::WriteFile(zulu, ""));

  ASSERT_TRUE(base::CreateSymbolicLink(zulu, timezone_symlink_));
  CallUpdateDeviceTz();
  EXPECT_EQ("Zulu", GetCommon()->local_timezone());
}

TEST_F(MessageSenderTestFixture, TestTzUpdateNotInZoneInfo) {
  const base::FilePath bad = fake_root_.GetPath().Append("IAmError");
  ASSERT_TRUE(base::WriteFile(bad, ""));

  ASSERT_TRUE(base::CreateSymbolicLink(bad, timezone_symlink_));
  CallUpdateDeviceTz();
  // Timezone isn't updated.
  EXPECT_EQ("", GetCommon()->local_timezone());
}
}  // namespace secagentd::testing
