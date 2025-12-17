// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/zram_writeback.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <absl/status/status.h>
#include <base/byte_size.h>
#include <base/test/task_environment.h>
#include <chromeos/dbus/swap_management/dbus-constants.h>
#include <gtest/gtest.h>

#include "base/time/time.h"
#include "swap_management/mock_utils.h"
#include "swap_management/suspend_history.h"

using testing::_;
using testing::DoAll;
using testing::ElementsAre;
using testing::InSequence;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace swap_management {
class MockZramWriteback : public swap_management::ZramWriteback {
 public:
  MockZramWriteback() = default;
  MockZramWriteback& operator=(const MockZramWriteback&) = delete;
  MockZramWriteback(const MockZramWriteback&) = delete;

  void PeriodicWriteback() {
    wb_size_bytes_ = 644874240;
    zram_nr_pages_ = 4072148;
    ZramWriteback::PeriodicWriteback();
  }

  absl::Status EnableWriteback(uint32_t size_mb) {
    return ZramWriteback::EnableWriteback(size_mb);
  }

  absl::Status SetZramWritebackConfigIfOverriden(const std::string& key,
                                                 const std::string& value) {
    return ZramWriteback::SetZramWritebackConfigIfOverriden(key, value);
  }

  uint64_t GetWritebackDailyLimit() {
    return ZramWriteback::GetWritebackDailyLimit();
  }

  void AddRecord(uint64_t wb_pages) { ZramWriteback::AddRecord(wb_pages); }
};

class ZramWritebackTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_zram_writeback_ = std::make_unique<MockZramWriteback>();
    // Init Utils and then replace with mocked one.
    Utils::OverrideForTesting(&mock_util_);
  }

 protected:
  // NOTE: mock_util_ is used by mock_zram_writeback_'s destructor. They must
  // be declared in this order so they are destructed in the correct sequence.
  MockUtils mock_util_;
  std::unique_ptr<MockZramWriteback> mock_zram_writeback_;
  base::test::TaskEnvironment task_environment{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};

  void ExpectWriteback(std::string mode,
                       std::optional<uint32_t> idle_duration_seconds,
                       uint32_t after_writeback_limit,
                       base::ByteSize available,
                       base::ByteSize total) {
    // GetCurrentIdleTimeSec
    if (idle_duration_seconds.has_value()) {
      base::SystemMemoryInfo mock_meminfo;
      mock_meminfo.available = available;
      mock_meminfo.total = total;
      EXPECT_CALL(mock_util_, GetSystemMemoryInfo())
          .WillOnce(Return(mock_meminfo));
      // MarkIdle
      EXPECT_CALL(mock_util_,
                  WriteFile(base::FilePath("/sys/block/zram0/idle"),
                            std::to_string(idle_duration_seconds.value())))
          .WillOnce(Return(absl::OkStatus()));
    }
    // InitiateWriteback
    EXPECT_CALL(mock_util_,
                WriteFile(base::FilePath("/sys/block/zram0/writeback"), mode))
        .WillOnce(Return(absl::OkStatus()));
    // GetWritebackLimit
    EXPECT_CALL(
        mock_util_,
        ReadFileToString(base::FilePath("/sys/block/zram0/writeback_limit"), _))
        .WillOnce(DoAll(
            SetArgPointee<1>(std::to_string(after_writeback_limit) + "\n"),
            Return(absl::OkStatus())));
  }
};

TEST_F(ZramWritebackTest, EnableWriteback) {
  // PrerequisiteCheck
  EXPECT_CALL(
      mock_util_,
      ReadFileToString(base::FilePath("/sys/block/zram0/backing_dev"), _))
      .WillOnce(DoAll(SetArgPointee<1>("none\n"), Return(absl::OkStatus())));
  EXPECT_CALL(mock_util_, DeleteFile(base::FilePath("/run/zram-integrity")))
      .WillOnce(Return(absl::OkStatus()));

  // GetWritebackInfo
  struct statfs sf = {
      .f_bsize = 4096,
      .f_blocks = 2038647,
      .f_bfree = 1051730,
  };
  EXPECT_CALL(
      mock_util_,
      GetStatfs("/mnt/stateful_partition/unencrypted/userspace_swap.tmp"))
      .WillOnce(Return(std::move(sf)));

  // CreateDmDevicesAndEnableWriteback
  EXPECT_CALL(
      mock_util_,
      WriteFile(base::FilePath("/mnt/stateful_partition/unencrypted/"
                               "userspace_swap.tmp/zram_writeback.swap"),
                std::string()))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_util_,
      Fallocate(base::FilePath("/mnt/stateful_partition/unencrypted/"
                               "userspace_swap.tmp/zram_writeback.swap"),
                645922816))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_util_,
      RunProcessHelper(ElementsAre("/sbin/losetup", "--show", "--direct-io=on",
                                   "--sector-size=4096", "-f",
                                   "/mnt/stateful_partition/unencrypted/"
                                   "userspace_swap.tmp/zram_writeback.swap"),
                       _))
      .WillOnce(
          DoAll(SetArgPointee<1>("/dev/loop10\n"), Return(absl::OkStatus())));
  EXPECT_CALL(mock_util_,
              CreateDirectory(base::FilePath("/run/zram-integrity")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_util_, SetPosixFilePermissions(
                              base::FilePath("/run/zram-integrity"), 0700))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_util_, Mount("none", "/run/zram-integrity", "ramfs", 0,
                                "noexec,nosuid,noatime,mode=0700"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_util_,
      WriteFile(base::FilePath("/run/zram-integrity/zram_integrity.swap"),
                std::string(4194304, 0)))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_util_,
      RunProcessHelper(ElementsAre("/sbin/losetup", "--show", "-f",
                                   "/run/zram-integrity/zram_integrity.swap"),
                       _))
      .WillOnce(
          DoAll(SetArgPointee<1>("/dev/loop11\n"), Return(absl::OkStatus())));

  EXPECT_CALL(
      mock_util_,
      RunProcessHelper(ElementsAre(
          "/sbin/dmsetup", "create", "zram-integrity", "--table",
          "0 1261568 integrity /dev/loop10 0 24 D 4 block_size:4096 "
          "meta_device:/dev/loop11 journal_sectors:1 buffer_sectors:128")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_util_,
              PathExists(base::FilePath("/dev/mapper/zram-integrity")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_util_, GenerateRandHex(32))
      .WillOnce(Return(std::move(
          "31EDB364E004FA99CFDBA21D726284A810421F7466F892ED2306DB7FB917084E")));
  EXPECT_CALL(mock_util_,
              RunProcessHelper(ElementsAre(
                  "/sbin/dmsetup", "create", "zram-writeback", "--table",
                  "0 1261568 crypt capi:gcm(aes)-random "
                  "31EDB364E004FA99CFDBA21D726284A810421F7466F892ED2306DB7FB917"
                  "084E 0 /dev/mapper/zram-integrity 0 4 allow_discards "
                  "submit_from_crypt_cpus sector_size:4096 integrity:24:aead")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_util_,
              PathExists(base::FilePath("/dev/mapper/zram-writeback")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_util_,
              WriteFile(base::FilePath("/sys/block/zram0/backing_dev"),
                        "/dev/mapper/zram-writeback"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_util_, DeleteFile(base::FilePath(
                              "/mnt/stateful_partition/unencrypted/"
                              "userspace_swap.tmp/zram_writeback.swap")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_util_,
      DeleteFile(base::FilePath("/run/zram-integrity/zram_integrity.swap")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_util_, RunProcessHelper(ElementsAre("/sbin/losetup", "-d",
                                                       "/dev/loop10")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_util_, RunProcessHelper(ElementsAre("/sbin/losetup", "-d",
                                                       "/dev/loop11")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_util_,
              RunProcessHelper(ElementsAre("/sbin/dmsetup", "remove",
                                           "--deferred", "zram-writeback")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_util_,
              RunProcessHelper(ElementsAre("/sbin/dmsetup", "remove",
                                           "--deferred", "zram-integrity")))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_THAT(mock_zram_writeback_->EnableWriteback(1024), absl::OkStatus());

  // Destructors may call mock functions on cleanup. Allow those calls to
  // happen without validating them against the above expectations.
  testing::Mock::VerifyAndClearExpectations(&mock_util_);
}

TEST_F(ZramWritebackTest, PeriodicWriteback) {
  InSequence s;

  // GetAllowedWritebackLimit
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/mm_stat"), _))
      .WillOnce(DoAll(
          SetArgPointee<1>("4760850432 1936262113 1973989376        0 "
                           "1975132160     8534     5979      530      531\n"),
          Return(absl::OkStatus())));
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/bd_stat"), _))
      .WillOnce(DoAll(SetArgPointee<1>("       1        0        1\n"),
                      Return(absl::OkStatus())));
  // SetWritebackLimit
  EXPECT_CALL(
      mock_util_,
      WriteFile(base::FilePath("/sys/block/zram0/writeback_limit_enable"), "1"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_util_,
      WriteFile(base::FilePath("/sys/block/zram0/writeback_limit"), "21918"))
      .WillOnce(Return(absl::OkStatus()));
  // GetWritebackLimit
  EXPECT_CALL(
      mock_util_,
      ReadFileToString(base::FilePath("/sys/block/zram0/writeback_limit"), _))
      .WillOnce(DoAll(SetArgPointee<1>("21918\n"), Return(absl::OkStatus())));

  ExpectWriteback("huge_idle", 72150, 8845, base::KiBU(346452),
                  base::KiBU(8144296));
  ExpectWriteback("idle", 72150, 5000, base::KiBU(348332), base::KiBU(8144296));
  ExpectWriteback("huge", std::nullopt, 5000, base::KiBU(348332),
                  base::KiBU(8144296));

  mock_zram_writeback_->PeriodicWriteback();
}

TEST_F(ZramWritebackTest, PeriodicWritebackSkipOnWriteLimit) {
  InSequence s;

  // GetAllowedWritebackLimit
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/mm_stat"), _))
      .WillOnce(DoAll(
          SetArgPointee<1>("4760850432 1936262113 1973989376        0 "
                           "1975132160     8534     5979      530      531\n"),
          Return(absl::OkStatus())));
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/bd_stat"), _))
      .WillOnce(DoAll(SetArgPointee<1>("       1        0        1\n"),
                      Return(absl::OkStatus())));
  // SetWritebackLimit
  EXPECT_CALL(
      mock_util_,
      WriteFile(base::FilePath("/sys/block/zram0/writeback_limit_enable"), "1"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_util_,
      WriteFile(base::FilePath("/sys/block/zram0/writeback_limit"), "21918"))
      .WillOnce(Return(absl::OkStatus()));
  // GetWritebackLimit
  EXPECT_CALL(
      mock_util_,
      ReadFileToString(base::FilePath("/sys/block/zram0/writeback_limit"), _))
      .WillOnce(DoAll(SetArgPointee<1>("21918\n"), Return(absl::OkStatus())));

  ExpectWriteback("huge_idle", 72150, 8845, base::KiBU(346452),
                  base::KiBU(8144296));
  ExpectWriteback("idle", 72150, 0, base::KiBU(348332), base::KiBU(8144296));
  // Skip huge page writeback since after_writeback_limit was 0.

  mock_zram_writeback_->PeriodicWriteback();
}

TEST_F(ZramWritebackTest, DailyLimit) {
  InSequence s;

  // Set daily limit to 128MiB.
  EXPECT_THAT(mock_zram_writeback_->SetZramWritebackConfigIfOverriden(
                  "max_pages_per_day", "32768"),
              absl::OkStatus());

  // 1st writeback (0s), history: {}, return 32768 pages.
  EXPECT_THAT(mock_zram_writeback_->GetWritebackDailyLimit(), 32768);
  // 10000 huge and 20000 idle pages were written back.
  mock_zram_writeback_->AddRecord(10000);
  mock_zram_writeback_->AddRecord(20000);

  // Mocked writeback runs every 6 hours.
  task_environment.AdvanceClock(base::Hours(6));

  // 2nd writeback (21600s), history: {(0, 10000), (0, 20000)}, return 2768
  // pages.
  EXPECT_THAT(mock_zram_writeback_->GetWritebackDailyLimit(), 2768);

  // 2000 pages were written back.
  mock_zram_writeback_->AddRecord(2000);

  task_environment.FastForwardBy(base::Hours(6));

  // 3rd writeback (43200s), history: {(0, 10000), (0, 20000), (21600, 2000)},
  // return 768 pages.
  EXPECT_THAT(mock_zram_writeback_->GetWritebackDailyLimit(), 768);
  // 500 pages were written back.
  mock_zram_writeback_->AddRecord(500);

  task_environment.FastForwardBy(base::Hours(6));

  // 4th writeback (64800s), history: {(0, 10000), (0, 20000), (21600, 2000),
  // (43200, 500)}, return 268 pages.
  EXPECT_THAT(mock_zram_writeback_->GetWritebackDailyLimit(), 268);
  // 0 pages were written back.
  mock_zram_writeback_->AddRecord(0);

  task_environment.FastForwardBy(base::Hours(6));

  // 5th writeback (86400s), history: {(21600, 2000), (43200, 500)}, return
  // 30268 pages.
  EXPECT_THAT(mock_zram_writeback_->GetWritebackDailyLimit(), 30268);
  // 30268 pages were written back.
  mock_zram_writeback_->AddRecord(30268);

  task_environment.FastForwardBy(base::Hours(6));

  // 6th writeback (108000s), history: {(43200, 500), (86400, 30268)}, return
  // 2000 pages.
  EXPECT_THAT(mock_zram_writeback_->GetWritebackDailyLimit(), 2000);
  // 2000 were written back.
  mock_zram_writeback_->AddRecord(2000);

  task_environment.FastForwardBy(base::Hours(6));

  // 7th writeback (129600s), history: {(43200, 500), (86400, 30268), (108000,
  // 2000)}, return 500 pages.
  EXPECT_THAT(mock_zram_writeback_->GetWritebackDailyLimit(), 500);
  // 500 pages were written back.
  mock_zram_writeback_->AddRecord(500);

  task_environment.FastForwardBy(base::Hours(6));

  // 7th writeback (151200s), history: {(86400, 30268), (108000, 2000), (129600,
  // 500)}, return 0 pages.
  EXPECT_THAT(mock_zram_writeback_->GetWritebackDailyLimit(), 0);
  // 0 page was written back.
  mock_zram_writeback_->AddRecord(0);

  task_environment.FastForwardBy(base::Hours(6));

  // 8th writeback (172800), history: {(108000, 2000), (129600, 500)}, return
  // 30268 pages.
  EXPECT_THAT(mock_zram_writeback_->GetWritebackDailyLimit(), 30268);
}

TEST_F(ZramWritebackTest, DisabledWhileSuspended) {
  InSequence s;

  EXPECT_THAT(mock_zram_writeback_->SetZramWritebackConfigIfOverriden(
                  "suspend_aware", "true"),
              absl::OkStatus());

  // GetAllowedWritebackLimit
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/mm_stat"), _))
      .Times(0);
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/bd_stat"), _))
      .Times(0);
  // SetWritebackLimit
  EXPECT_CALL(
      mock_util_,
      WriteFile(base::FilePath("/sys/block/zram0/writeback_limit_enable"), _))
      .Times(0);
  EXPECT_CALL(
      mock_util_,
      ReadFileToString(base::FilePath("/sys/block/zram0/writeback_limit"), _))
      .Times(0);
  EXPECT_CALL(mock_util_, GetSystemMemoryInfo()).Times(0);
  // MarkIdle
  EXPECT_CALL(mock_util_, WriteFile(base::FilePath("/sys/block/zram0/idle"), _))
      .Times(0);
  // InitiateWriteback
  EXPECT_CALL(mock_util_,
              WriteFile(base::FilePath("/sys/block/zram0/writeback"), _))
      .Times(0);

  SuspendHistory::Get()->OnSuspendImminent();
  mock_zram_writeback_->PeriodicWriteback();
}

TEST_F(ZramWritebackTest, AdjustThresholdBySuspendTime) {
  InSequence s;

  EXPECT_THAT(mock_zram_writeback_->SetZramWritebackConfigIfOverriden(
                  "suspend_aware", "true"),
              absl::OkStatus());

  // GetAllowedWritebackLimit
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/mm_stat"), _))
      .WillOnce(DoAll(
          SetArgPointee<1>("4760850432 1936262113 1973989376        0 "
                           "1975132160     8534     5979      530      531\n"),
          Return(absl::OkStatus())));
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/bd_stat"), _))
      .WillOnce(DoAll(SetArgPointee<1>("       1        0        1\n"),
                      Return(absl::OkStatus())));
  // SetWritebackLimit
  EXPECT_CALL(
      mock_util_,
      WriteFile(base::FilePath("/sys/block/zram0/writeback_limit_enable"), "1"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      mock_util_,
      WriteFile(base::FilePath("/sys/block/zram0/writeback_limit"), "21918"))
      .WillOnce(Return(absl::OkStatus()));
  // GetWritebackLimit
  EXPECT_CALL(
      mock_util_,
      ReadFileToString(base::FilePath("/sys/block/zram0/writeback_limit"), _))
      .WillOnce(DoAll(SetArgPointee<1>("21918\n"), Return(absl::OkStatus())));

  // MarkIdle 75750 (= 72150 + 3600)
  ExpectWriteback("huge_idle", 75750, 0, base::KiBU(346452),
                  base::KiBU(8144296));

  SuspendHistory::Get()->OnSuspendImminent();
  SuspendHistory::Get()->OnSuspendDone(base::Hours(1));
  mock_zram_writeback_->PeriodicWriteback();
}

}  // namespace swap_management
