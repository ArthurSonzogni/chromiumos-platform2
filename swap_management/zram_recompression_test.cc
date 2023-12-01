// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/mock_utils.h"
#include "swap_management/zram_recompression.h"

#include <memory>

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
class MockZramRecompression : public swap_management::ZramRecompression {
 public:
  MockZramRecompression() = default;
  MockZramRecompression& operator=(const MockZramRecompression&) = delete;
  MockZramRecompression(const MockZramRecompression&) = delete;

  void PeriodicRecompress() { ZramRecompression::Get()->PeriodicRecompress(); }
};

class ZramRecompressionTest : public ::testing::Test {
 public:
  void SetUp() override {
    mock_zram_writeback_ = std::make_unique<MockZramRecompression>();
    // Init Utils and then replace with mocked one.
    Utils::OverrideForTesting(&mock_util_);
  }

 protected:
  std::unique_ptr<MockZramRecompression> mock_zram_writeback_;
  MockUtils mock_util_;
};

TEST_F(ZramRecompressionTest, PeriodicRecompress) {
  InSequence s;

  // huge_idle
  // GetCurrentIdleTimeSec
  base::SystemMemoryInfoKB mock_meminfo;
  mock_meminfo.available = 346452;
  mock_meminfo.total = 8144296;
  EXPECT_CALL(mock_util_, GetSystemMemoryInfo()).WillOnce(Return(mock_meminfo));
  // MarkIdle
  EXPECT_CALL(mock_util_,
              WriteFile(base::FilePath("/sys/block/zram0/idle"), "3750"))
      .WillOnce(Return(absl::OkStatus()));
  // InitiateWriteback
  EXPECT_CALL(mock_util_,
              WriteFile(base::FilePath("/sys/block/zram0/recompress"),
                        "type=huge_idle threshold=1024"))
      .WillOnce(Return(absl::OkStatus()));
  // idle
  // GetCurrentIdleTimeSec
  mock_meminfo.available = 348332;
  EXPECT_CALL(mock_util_, GetSystemMemoryInfo()).WillOnce(Return(mock_meminfo));
  // MarkIdle
  EXPECT_CALL(mock_util_,
              WriteFile(base::FilePath("/sys/block/zram0/idle"), "3750"))
      .WillOnce(Return(absl::OkStatus()));
  // InitiateWriteback
  EXPECT_CALL(mock_util_,
              WriteFile(base::FilePath("/sys/block/zram0/recompress"),
                        "type=idle threshold=1024"))
      .WillOnce(Return(absl::OkStatus()));
  // huge
  // InitiateWriteback
  EXPECT_CALL(mock_util_,
              WriteFile(base::FilePath("/sys/block/zram0/recompress"),
                        "type=huge threshold=1024"))
      .WillOnce(Return(absl::OkStatus()));

  mock_zram_writeback_->PeriodicRecompress();
}

}  // namespace swap_management
