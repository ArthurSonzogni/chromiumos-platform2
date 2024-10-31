// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/swap_tool.h"

#include <memory>

#include <absl/status/status.h>
#include <absl/strings/str_cat.h>
#include <chromeos/dbus/swap_management/dbus-constants.h>
#include <gtest/gtest.h>

#include "gmock/gmock.h"
#include "swap_management/mock_metrics.h"
#include "swap_management/mock_utils.h"

using testing::_;
using testing::DoAll;
using testing::ElementsAre;
using testing::Return;
using testing::SetArgPointee;
using testing::StrictMock;

namespace swap_management {
namespace {
const char kSwapsNoZram[] =
    "Filename                               "
    " Type            Size            "
    "Used            Priority\n";
const char kZramDisksize8G[] = "16679780352";
const int kZramMemTotal8G = 8144424;
}  // namespace

class SwapToolTest : public ::testing::Test {
 public:
  void SetUp() override {
    // Init Utils and then replace with mocked one.
    Utils::OverrideForTesting(&mock_util_);
  }

 protected:
  MockUtils mock_util_;
};

TEST_F(SwapToolTest, SwapIsAlreadyOnOrOff) {
  SwapTool swap_tool(nullptr);
  EXPECT_CALL(mock_util_, ReadFileToString(base::FilePath("/proc/swaps"), _))
      .WillOnce(DoAll(SetArgPointee<1>(
                          absl::StrCat(kSwapsNoZram,
                                       "/dev/zram0                             "
                                       " partition       16288844        "
                                       "0               -2\n")),
                      Return(absl::OkStatus())));
  EXPECT_THAT(swap_tool.SwapStart(), absl::OkStatus());

  EXPECT_CALL(mock_util_, ReadFileToString(base::FilePath("/proc/swaps"), _))
      .WillOnce(DoAll(
          SetArgPointee<1>(absl::StrCat(kSwapsNoZram,
                                        "/zram0                              "
                                        "partition       16288844        "
                                        "0               -2\n")),
          Return(absl::OkStatus())));
  EXPECT_THAT(swap_tool.SwapStart(), absl::OkStatus());

  EXPECT_CALL(mock_util_, ReadFileToString(base::FilePath("/proc/swaps"), _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kSwapsNoZram), Return(absl::OkStatus())));
  EXPECT_THAT(swap_tool.SwapStop(), absl::OkStatus());
}

TEST_F(SwapToolTest, SwapStart) {
  SwapTool swap_tool(nullptr);
  // IsZramSwapOn
  EXPECT_CALL(mock_util_, ReadFileToString(base::FilePath("/proc/swaps"), _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kSwapsNoZram), Return(absl::OkStatus())));
  // GetZramSizeBytes
  // GetUserConfigZramSizeBytes
  EXPECT_CALL(mock_util_, ReadFileToStringWithMaxSize(
                              base::FilePath("/var/lib/swap/swap_size"), _, _))
      .WillOnce(Return(
          absl::NotFoundError("Failed to read /var/lib/swap/swap_size")));
  base::SystemMemoryInfoKB mock_meminfo;
  mock_meminfo.total = kZramMemTotal8G;
  EXPECT_CALL(mock_util_, GetSystemMemoryInfo()).WillOnce(Return(mock_meminfo));
  EXPECT_CALL(mock_util_,
              RunProcessHelper(ElementsAre("/usr/bin/modprobe", "zram")))
      .WillOnce(Return(absl::OkStatus()));
  // EnableZramRecompression, we don't test it here.
  EXPECT_CALL(mock_util_,
              PathExists(base::FilePath("/sys/block/zram0/recomp_algorithm")))
      .WillOnce(Return(absl::NotFoundError(
          "/sys/block/zram0/recomp_algorithm does not exist.")));
  EXPECT_CALL(mock_util_, WriteFile(base::FilePath("/sys/block/zram0/disksize"),
                                    kZramDisksize8G))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_util_,
              RunProcessHelper(ElementsAre("/sbin/mkswap", "/dev/zram0")))
      .WillOnce(Return(absl::OkStatus()));
  // EnableZramSwapping
  EXPECT_CALL(mock_util_,
              RunProcessHelper(ElementsAre("/sbin/swapon", "/dev/zram0")))
      .WillOnce(Return(absl::OkStatus()));
  // Metrics::Get()->Start()
  // We don't test metrics in swap_tool_test, just simply return.
  MockMetrics mock_metrics_;
  Metrics::OverrideForTesting(&mock_metrics_);
  EXPECT_CALL(mock_metrics_, Start()).WillOnce(Return());

  EXPECT_THAT(swap_tool.SwapStart(), absl::OkStatus());
}

TEST_F(SwapToolTest, SwapStartButSwapIsDisabled) {
  SwapTool swap_tool(nullptr);
  // IsZramSwapOn
  EXPECT_CALL(mock_util_, ReadFileToString(base::FilePath("/proc/swaps"), _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kSwapsNoZram), Return(absl::OkStatus())));
  // GetZramSizeBytes
  // GetUserConfigZramSizeBytes
  EXPECT_CALL(mock_util_, ReadFileToStringWithMaxSize(
                              base::FilePath("/var/lib/swap/swap_size"), _, _))
      .WillOnce(DoAll(SetArgPointee<1>("0"), Return(absl::OkStatus())));

  EXPECT_THAT(swap_tool.SwapStart(), absl::OkStatus());
}

TEST_F(SwapToolTest, SwapStop) {
  SwapTool swap_tool(nullptr);
  // IsZramSwapOn
  EXPECT_CALL(mock_util_, ReadFileToString(base::FilePath("/proc/swaps"), _))
      .WillOnce(DoAll(
          SetArgPointee<1>(absl::StrCat(std::string(kSwapsNoZram),
                                        "/zram0                              "
                                        "partition       16288844        "
                                        "0               -2\n")),
          Return(absl::OkStatus())));
  EXPECT_CALL(mock_util_, RunProcessHelper(
                              ElementsAre("/sbin/swapoff", "-v", "/dev/zram0")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_util_, WriteFile(base::FilePath("/sys/block/zram0/reset"),
                                    std::to_string(1)))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_THAT(swap_tool.SwapStop(), absl::OkStatus());
}

TEST_F(SwapToolTest, SwapSetSize) {
  SwapTool swap_tool(nullptr);
  // If size is negative.
  EXPECT_CALL(mock_util_, WriteFile(base::FilePath("/var/lib/swap/swap_size"),
                                    absl::StrCat(0)))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_THAT(swap_tool.SwapSetSize(-1), absl::OkStatus());

  // If size is 0.
  EXPECT_CALL(mock_util_, DeleteFile(base::FilePath("/var/lib/swap/swap_size")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_THAT(swap_tool.SwapSetSize(0), absl::OkStatus());

  // If size is larger than 65000.
  absl::Status status = swap_tool.SwapSetSize(128000);
  EXPECT_TRUE(absl::IsInvalidArgument(status));
  EXPECT_EQ(status.ToString(),
            "INVALID_ARGUMENT: Size is not between 128 and 65000 MiB.");

  // If size is smaller than 128, but not 0.
  status = swap_tool.SwapSetSize(64);
  EXPECT_TRUE(absl::IsInvalidArgument(status));
  EXPECT_EQ(status.ToString(),
            "INVALID_ARGUMENT: Size is not between 128 and 65000 MiB.");

  // If size is between 128 and 65000.
  EXPECT_CALL(mock_util_, WriteFile(base::FilePath("/var/lib/swap/swap_size"),
                                    absl::StrCat(1024)))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_THAT(swap_tool.SwapSetSize(1024), absl::OkStatus());
}

}  // namespace swap_management
