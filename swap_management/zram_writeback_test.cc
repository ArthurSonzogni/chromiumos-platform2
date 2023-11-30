// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/mock_utils.h"
#include "swap_management/zram_writeback.h"

#include <utility>

#include <absl/status/status.h>
#include <chromeos/dbus/swap_management/dbus-constants.h>
#include <gtest/gtest.h>

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

  absl::Status EnableWriteback(uint32_t size_mb) {
    return ZramWriteback::Get()->EnableWriteback(size_mb);
  }

  void PeriodicWriteback() {
    ZramWriteback::Get()->wb_size_bytes_ = 644874240;
    ZramWriteback::Get()->zram_nr_pages_ = 4072148;
    ZramWriteback::Get()->PeriodicWriteback();
  }
};

class ZramWritebackTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_zram_writeback_ = std::make_unique<MockZramWriteback>();
    // Init Utils and then replace with mocked one.
    Utils::OverrideForTesting(&mock_util_);
  }

 protected:
  std::unique_ptr<MockZramWriteback> mock_zram_writeback_;
  MockUtils mock_util_;
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
      WriteFile(base::FilePath("/sys/block/zram0/writeback_limit"), "9351"))
      .WillOnce(Return(absl::OkStatus()));
  // GetWritebackLimit
  EXPECT_CALL(
      mock_util_,
      ReadFileToString(base::FilePath("/sys/block/zram0/writeback_limit"), _))
      .WillOnce(DoAll(SetArgPointee<1>("9351\n"), Return(absl::OkStatus())));
  // huge_idle
  // GetCurrentIdleTimeSec
  base::SystemMemoryInfoKB mock_meminfo;
  mock_meminfo.available = 346452;
  mock_meminfo.total = 8144296;
  EXPECT_CALL(mock_util_, GetSystemMemoryInfo()).WillOnce(Return(mock_meminfo));
  // MarkIdle
  EXPECT_CALL(mock_util_,
              WriteFile(base::FilePath("/sys/block/zram0/idle"), "839"))
      .WillOnce(Return(absl::OkStatus()));
  // InitiateWriteback
  EXPECT_CALL(
      mock_util_,
      WriteFile(base::FilePath("/sys/block/zram0/writeback"), "huge_idle"))
      .WillOnce(Return(absl::OkStatus()));
  // GetWritebackLimit
  EXPECT_CALL(
      mock_util_,
      ReadFileToString(base::FilePath("/sys/block/zram0/writeback_limit"), _))
      .WillOnce(DoAll(SetArgPointee<1>("8845\n"), Return(absl::OkStatus())));
  // idle
  // GetCurrentIdleTimeSec
  mock_meminfo.available = 348332;
  EXPECT_CALL(mock_util_, GetSystemMemoryInfo()).WillOnce(Return(mock_meminfo));
  // MarkIdle
  EXPECT_CALL(mock_util_,
              WriteFile(base::FilePath("/sys/block/zram0/idle"), "839"))
      .WillOnce(Return(absl::OkStatus()));
  // InitiateWriteback
  EXPECT_CALL(mock_util_,
              WriteFile(base::FilePath("/sys/block/zram0/writeback"), "idle"))
      .WillOnce(Return(absl::UnknownError("Error: Input/output error Failed to "
                                          "write /sys/block/zram0/writeback")));
  // GetWritebackLimit
  EXPECT_CALL(
      mock_util_,
      ReadFileToString(base::FilePath("/sys/block/zram0/writeback_limit"), _))
      .WillOnce(DoAll(SetArgPointee<1>("0\n"), Return(absl::OkStatus())));

  mock_zram_writeback_->PeriodicWriteback();
}

}  // namespace swap_management
