// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/zram_stats.h"

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <gtest/gtest.h>

#include "swap_management/mock_utils.h"

using testing::_;
using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace swap_management {

class ZramStatsTest : public ::testing::Test {
 public:
  void SetUp() override {
    // Init Utils and then replace with mocked one.
    Utils::OverrideForTesting(&mock_util_);
  }

 protected:
  MockUtils mock_util_;
};

TEST_F(ZramStatsTest, ZramMmStat) {
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/mm_stat"), _))
      .WillOnce(DoAll(SetArgPointee<1>(absl::StrCat(
                          "    4096       74    12288        0    12288        "
                          "0        0        0        0\n")),
                      Return(absl::OkStatus())));
  absl::StatusOr<ZramMmStat> zram_mm_stat = GetZramMmStat();
  EXPECT_THAT(zram_mm_stat.status(), absl::OkStatus());
  ASSERT_EQ((*zram_mm_stat).orig_data_size, 4096u);
  ASSERT_EQ((*zram_mm_stat).compr_data_size, 74u);
  ASSERT_EQ((*zram_mm_stat).mem_used_total, 12288u);
  ASSERT_EQ((*zram_mm_stat).mem_limit, 0u);
  ASSERT_EQ((*zram_mm_stat).mem_used_max, 12288u);
  ASSERT_EQ((*zram_mm_stat).same_pages, 0u);
  ASSERT_EQ((*zram_mm_stat).pages_compacted, 0u);
  ASSERT_EQ((*zram_mm_stat).huge_pages, 0u);
  ASSERT_EQ((*zram_mm_stat).huge_pages_since, 0u);

  // mm_stat only contains number.
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/mm_stat"), _))
      .WillOnce(DoAll(
          SetArgPointee<1>(absl::StrCat("    aa4096    bb74    122e8  gg0    "
                                        "12288        0        0        0    "
                                        "    0\n")),
          Return(absl::OkStatus())));
  zram_mm_stat = GetZramMmStat();
  EXPECT_THAT(zram_mm_stat.status(),
              absl::InvalidArgumentError("Failed to parse zram mm_stat"));

  // mm_stat contains at least 7 items.
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/mm_stat"), _))
      .WillOnce(DoAll(
          SetArgPointee<1>(absl::StrCat("    0        0        0        0\n")),
          Return(absl::OkStatus())));
  zram_mm_stat = GetZramMmStat();
  EXPECT_THAT(zram_mm_stat.status(),
              absl::InvalidArgumentError("Malformed zram mm_stat input"));

  // The fifth item in mm_stat must be positive.
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/mm_stat"), _))
      .WillOnce(DoAll(SetArgPointee<1>(absl::StrCat(
                          "    4096       74    12288        0    -12288       "
                          " 0        0      0      0\n")),
                      Return(absl::OkStatus())));
  zram_mm_stat = GetZramMmStat();
  EXPECT_THAT(zram_mm_stat.status(),
              absl::InvalidArgumentError("Bad value for zram max_used_pages"));
}

TEST_F(ZramStatsTest, ZramBdStat) {
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/bd_stat"), _))
      .WillOnce(
          DoAll(SetArgPointee<1>(absl::StrCat("     464        0      464\n")),
                Return(absl::OkStatus())));
  absl::StatusOr<ZramBdStat> zram_bd_stat = GetZramBdStat();
  EXPECT_THAT(zram_bd_stat.status(), absl::OkStatus());
  ASSERT_EQ((*zram_bd_stat).bd_count, 464u);
  ASSERT_EQ((*zram_bd_stat).bd_reads, 0u);
  ASSERT_EQ((*zram_bd_stat).bd_writes, 464u);

  // bd_stat only contains number.
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/bd_stat"), _))
      .WillOnce(
          DoAll(SetArgPointee<1>(absl::StrCat("    aa4096    bb74    122e8\n")),
                Return(absl::OkStatus())));
  zram_bd_stat = GetZramBdStat();
  EXPECT_THAT(zram_bd_stat.status(),
              absl::InvalidArgumentError("Failed to parse zram bd_stat"));

  // bd_stat contains at least 3 items.
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/bd_stat"), _))
      .WillOnce(DoAll(SetArgPointee<1>(absl::StrCat("    0        0\n")),
                      Return(absl::OkStatus())));
  zram_bd_stat = GetZramBdStat();
  EXPECT_THAT(zram_bd_stat.status(),
              absl::InvalidArgumentError("Malformed zram bd_stat input"));
}

TEST_F(ZramStatsTest, ZramIoStat) {
  EXPECT_CALL(mock_util_,
              ReadFileToString(base::FilePath("/sys/block/zram0/io_stat"), _))
      .WillOnce(DoAll(SetArgPointee<1>(absl::StrCat(
                          "       1        12        5        189\n")),
                      Return(absl::OkStatus())));
  absl::StatusOr<ZramIoStat> zram_io_stat = GetZramIoStat();
  EXPECT_THAT(zram_io_stat.status(), absl::OkStatus());
  ASSERT_EQ((*zram_io_stat).failed_reads, 1u);
  ASSERT_EQ((*zram_io_stat).failed_writes, 12u);
  ASSERT_EQ((*zram_io_stat).invalid_io, 5u);
  ASSERT_EQ((*zram_io_stat).notify_free, 189u);
}
}  // namespace swap_management
