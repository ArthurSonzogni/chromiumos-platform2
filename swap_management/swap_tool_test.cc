// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "swap_management/swap_tool.h"

#include <limits>

#include <absl/random/random.h>
#include <absl/strings/str_cat.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::_;
using testing::DoAll;
using testing::ElementsAre;
using testing::Return;
using testing::SetArgPointee;

namespace swap_management {

namespace {
const char kSwapsNoZram[] =
    "Filename                               "
    " Type            Size            "
    "Used            Priority\n";
const char kMeminfoMemTotal8G[] = R"(MemTotal:        8144424 kB
MemFree:         5712260 kB
MemAvailable:    6368308 kB
Buffers:           64092 kB
Cached:          1045820 kB
SwapCached:            0 kB
Active:          1437424 kB
Inactive:         493512 kB
Active(anon):     848464 kB
Inactive(anon):    63600 kB
Active(file):     588960 kB
Inactive(file):   429912 kB
Unevictable:       68676 kB
Mlocked:           40996 kB
SwapTotal:      16288844 kB
SwapFree:       16288844 kB
Dirty:               380 kB
Writeback:             0 kB
AnonPages:        889736 kB
Mapped:           470060 kB
Shmem:             91040 kB
KReclaimable:      87488 kB
Slab:             174412 kB
SReclaimable:      87488 kB
SUnreclaim:        86924 kB
KernelStack:        9056 kB
PageTables:        18120 kB
NFS_Unstable:          0 kB
Bounce:                0 kB
WritebackTmp:          0 kB
CommitLimit:    20361056 kB
Committed_AS:    5581424 kB
VmallocTotal:   34359738367 kB
VmallocUsed:      145016 kB
VmallocChunk:          0 kB
Percpu:             2976 kB
AnonHugePages:     40960 kB
ShmemHugePages:        0 kB
ShmemPmdMapped:        0 kB
FileHugePages:         0 kB
FilePmdMapped:         0 kB
DirectMap4k:      188268 kB
DirectMap2M:     8200192 kB)";
const char kZramDisksize8G[] = "16679780352";
const char kChromeosLowMemMargin8G[] = "413 3181";
}  // namespace

class MockSwapTool : public swap_management::SwapTool {
 public:
  MockSwapTool() : SwapTool() {}
  MOCK_METHOD(absl::Status,
              RunProcessHelper,
              (const std::vector<std::string>& commands),
              (override));
  MOCK_METHOD(absl::Status,
              WriteFile,
              (const base::FilePath& path, const std::string& data),
              (override));
  MOCK_METHOD(absl::Status,
              ReadFileToStringWithMaxSize,
              (const base::FilePath& path,
               std::string* contents,
               size_t max_size),
              (override));
  MOCK_METHOD(absl::Status,
              ReadFileToString,
              (const base::FilePath& path, std::string* contents),
              (override));
  MOCK_METHOD(absl::Status,
              DeleteFile,
              (const base::FilePath& path),
              (override));
};

TEST(SwapToolTest, SwapIsAlreadyOnOrOff) {
  MockSwapTool swap_tool;

  EXPECT_CALL(swap_tool, ReadFileToString(base::FilePath("/proc/swaps"), _))
      .WillOnce(DoAll(SetArgPointee<1>(
                          absl::StrCat(kSwapsNoZram,
                                       "/dev/zram0                             "
                                       " partition       16288844        "
                                       "0               -2\n")),
                      Return(absl::OkStatus())));
  EXPECT_THAT(swap_tool.SwapStart(), absl::OkStatus());

  EXPECT_CALL(swap_tool, ReadFileToString(base::FilePath("/proc/swaps"), _))
      .WillOnce(DoAll(
          SetArgPointee<1>(absl::StrCat(kSwapsNoZram,
                                        "/zram0                              "
                                        "partition       16288844        "
                                        "0               -2\n")),
          Return(absl::OkStatus())));
  EXPECT_THAT(swap_tool.SwapStart(), absl::OkStatus());

  EXPECT_CALL(swap_tool, ReadFileToString(base::FilePath("/proc/swaps"), _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kSwapsNoZram), Return(absl::OkStatus())));
  EXPECT_THAT(swap_tool.SwapStop(), absl::OkStatus());
}

TEST(SwapToolTest, SwapStartWithoutSwapEnabled) {
  MockSwapTool swap_tool;

  // IsZramSwapOn
  EXPECT_CALL(swap_tool, ReadFileToString(base::FilePath("/proc/swaps"), _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kSwapsNoZram), Return(absl::OkStatus())));
  // GetMemTotal
  EXPECT_CALL(swap_tool, ReadFileToString(base::FilePath("/proc/meminfo"), _))
      .WillOnce(DoAll(SetArgPointee<1>(kMeminfoMemTotal8G),
                      Return(absl::OkStatus())));
  // InitializeMMTunables
  EXPECT_CALL(
      swap_tool,
      WriteFile(base::FilePath("/sys/kernel/mm/chromeos-low_mem/margin"),
                kChromeosLowMemMargin8G))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      swap_tool,
      WriteFile(base::FilePath("/proc/sys/vm/min_filelist_kbytes"), "1000000"))
      .WillOnce(Return(absl::OkStatus()));
  // GetZramSize
  EXPECT_CALL(swap_tool, ReadFileToStringWithMaxSize(
                             base::FilePath("/var/lib/swap/swap_size"), _, _))
      .WillOnce(Return(
          absl::NotFoundError("Failed to read /var/lib/swap/swap_size")));
  EXPECT_CALL(swap_tool,
              RunProcessHelper(ElementsAre("/sbin/modprobe", "zram")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(swap_tool, WriteFile(base::FilePath("/sys/block/zram0/disksize"),
                                   kZramDisksize8G))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(swap_tool,
              RunProcessHelper(ElementsAre("/sbin/mkswap", "/dev/zram0")))
      .WillOnce(Return(absl::OkStatus()));
  // EnableZramSwapping
  EXPECT_CALL(swap_tool,
              RunProcessHelper(ElementsAre("/sbin/swapon", "/dev/zram0")))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_THAT(swap_tool.SwapStart(), absl::OkStatus());
}

TEST(SwapToolTest, SwapStartButSwapIsDisabled) {
  MockSwapTool swap_tool;

  // IsZramSwapOn
  EXPECT_CALL(swap_tool, ReadFileToString(base::FilePath("/proc/swaps"), _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kSwapsNoZram), Return(absl::OkStatus())));
  // GetMemTotal
  EXPECT_CALL(swap_tool, ReadFileToString(base::FilePath("/proc/meminfo"), _))
      .WillOnce(DoAll(SetArgPointee<1>(kMeminfoMemTotal8G),
                      Return(absl::OkStatus())));
  // InitializeMMTunables
  EXPECT_CALL(
      swap_tool,
      WriteFile(base::FilePath("/sys/kernel/mm/chromeos-low_mem/margin"),
                kChromeosLowMemMargin8G))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      swap_tool,
      WriteFile(base::FilePath("/proc/sys/vm/min_filelist_kbytes"), "1000000"))
      .WillOnce(Return(absl::OkStatus()));
  // GetZramSize
  EXPECT_CALL(swap_tool, ReadFileToStringWithMaxSize(
                             base::FilePath("/var/lib/swap/swap_size"), _, _))
      .WillOnce(DoAll(SetArgPointee<1>("0"), Return(absl::OkStatus())));

  absl::Status status = swap_tool.SwapStart();
  EXPECT_TRUE(absl::IsInvalidArgument(status));
  EXPECT_EQ(status.ToString(),
            "INVALID_ARGUMENT: Swap is not turned on since "
            "/var/lib/swap/swap_size contains 0.");
}

TEST(SwapToolTest, SwapStartWithEmptySwapZramSize) {
  MockSwapTool swap_tool;

  // IsZramSwapOn
  EXPECT_CALL(swap_tool, ReadFileToString(base::FilePath("/proc/swaps"), _))
      .WillOnce(
          DoAll(SetArgPointee<1>(kSwapsNoZram), Return(absl::OkStatus())));
  // GetMemTotal
  EXPECT_CALL(swap_tool, ReadFileToString(base::FilePath("/proc/meminfo"), _))
      .WillOnce(DoAll(SetArgPointee<1>(kMeminfoMemTotal8G),
                      Return(absl::OkStatus())));
  // InitializeMMTunables
  EXPECT_CALL(
      swap_tool,
      WriteFile(base::FilePath("/sys/kernel/mm/chromeos-low_mem/margin"),
                kChromeosLowMemMargin8G))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(
      swap_tool,
      WriteFile(base::FilePath("/proc/sys/vm/min_filelist_kbytes"), "1000000"))
      .WillOnce(Return(absl::OkStatus()));
  // GetZramSize
  EXPECT_CALL(swap_tool, ReadFileToStringWithMaxSize(
                             base::FilePath("/var/lib/swap/swap_size"), _, _))
      .WillOnce(DoAll(SetArgPointee<1>(""), Return(absl::OkStatus())));
  EXPECT_CALL(swap_tool,
              RunProcessHelper(ElementsAre("/sbin/modprobe", "zram")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(swap_tool, WriteFile(base::FilePath("/sys/block/zram0/disksize"),
                                   kZramDisksize8G))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(swap_tool,
              RunProcessHelper(ElementsAre("/sbin/mkswap", "/dev/zram0")))
      .WillOnce(Return(absl::OkStatus()));
  // EnableZramSwapping
  EXPECT_CALL(swap_tool,
              RunProcessHelper(ElementsAre("/sbin/swapon", "/dev/zram0")))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_THAT(swap_tool.SwapStart(), absl::OkStatus());
}

TEST(SwapToolTest, SwapStop) {
  MockSwapTool swap_tool;

  // IsZramSwapOn
  EXPECT_CALL(swap_tool, ReadFileToString(base::FilePath("/proc/swaps"), _))
      .WillOnce(DoAll(
          SetArgPointee<1>(absl::StrCat(std::string(kSwapsNoZram),
                                        "/zram0                              "
                                        "partition       16288844        "
                                        "0               -2\n")),
          Return(absl::OkStatus())));
  EXPECT_CALL(swap_tool, RunProcessHelper(
                             ElementsAre("/sbin/swapoff", "-v", "/dev/zram0")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(swap_tool, WriteFile(base::FilePath("/sys/block/zram0/reset"),
                                   std::to_string(1)))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_THAT(swap_tool.SwapStop(), absl::OkStatus());
}

TEST(SwapToolTest, SwapSetSize) {
  MockSwapTool swap_tool;

  // If size is 0.
  EXPECT_CALL(swap_tool, DeleteFile(base::FilePath("/var/lib/swap/swap_size")))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_THAT(swap_tool.SwapSetSize(0), absl::OkStatus());

  // If size is larger than 20000.
  absl::BitGen gen;
  uint32_t size = absl::uniform_int_distribution<uint32_t>(
      20001, std::numeric_limits<uint32_t>::max())(gen);
  absl::Status status = swap_tool.SwapSetSize(size);
  EXPECT_TRUE(absl::IsInvalidArgument(status));
  EXPECT_EQ(status.ToString(),
            "INVALID_ARGUMENT: Size is not between 100 and 20000 MiB.");

  // If size is smaller than 100, but not 0.
  size = absl::uniform_int_distribution<uint32_t>(1, 99)(gen);
  status = swap_tool.SwapSetSize(size);
  EXPECT_TRUE(absl::IsInvalidArgument(status));
  EXPECT_EQ(status.ToString(),
            "INVALID_ARGUMENT: Size is not between 100 and 20000 MiB.");

  // If size is between 100 and 20000.
  size = absl::uniform_int_distribution<uint32_t>(100, 20000)(gen);
  EXPECT_CALL(swap_tool, WriteFile(base::FilePath("/var/lib/swap/swap_size"),
                                   absl::StrCat(size)))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_THAT(swap_tool.SwapSetSize(size), absl::OkStatus());
}

}  // namespace swap_management
