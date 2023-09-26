// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/virtio_blk_metrics.h"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

#include <base/time/time.h>
#include <base/timer/mock_timer.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library.h>
#include <metrics/metrics_library_mock.h>
#include <vm_applications/apps.pb.h>

using ::testing::_;
using ::testing::StrictMock;

namespace vm_tools::concierge {

using Files = std::map<const std::string, std::string>;

class VirtioBlkMetricsTest : public ::testing::Test {};

class FakeGuestFileReader : public VshFileReader {
 public:
  explicit FakeGuestFileReader(Files faked_files)
      : faked_files(std::move(faked_files)) {}

  std::optional<std::string> Read(uint32_t cid,
                                  const std::string& path) const override {
    return faked_files.at(path);
  }

  std::optional<bool> CheckIfExists(uint32_t cid,
                                    const std::string& path) const override {
    return faked_files.find(path) != faked_files.end();
  }

  Files faked_files;
};

TEST_F(VirtioBlkMetricsTest, ReportMetricsReportsCorrectMetrics) {
  StrictMock<MetricsLibraryMock> metrics_library;

  const std::vector<std::string> disks{"vda", "vde"};
  // Example stats taken from a DUT
  const Files files{
      {"/sys/block/vda/stat",
       // read_ios, r_merges, r_sectors, r_ticks, write_ios, w_merges, w_sectors
       "     6300        0  1158096     8398        0        0        0"
       // w_ticks, in_flight, io_ticks, time_in_queue, discard_ios,
       "        0        0     9244     8398        0"
       // d_merges, d_sectors, d_ticks, flush_ios, f_ticks
       "        0        0        0        0        0\n"},
      {"/sys/block/vde/stat",
       // read_ios, r_merges, r_sectors, r_ticks, write_ios, w_merges, w_sectors
       "       86        0     1752       74    22229    45384  2422272"
       // w_ticks, in_flight, io_ticks, time_in_queue, discard_ios,
       "    85922        0    52628   132196     3637"
       // d_merges, d_sectors, d_ticks, flush_ios, f_ticks
       "      685 178300312     3345    10814    42854\n"},
  };

  const char* name_prefix = "Disk";
  const std::tuple<const char*, int> expected[] = {
      {"Disk.IoTicks", 9244 + 52628},
      {"Disk.IoSize",
       static_cast<int>((1158096L + 1752 + 2422272) * 512 / 1024 / 1024)},
      {"Disk.IoCount", 6300 + 86 + 22229 + 3637 + 10814},
      {"Disk.KbPerTicks",
       static_cast<int>((1158096L + 1752 + 2422272) * 512 / 1024 / 61872)},
  };

  VirtioBlkMetrics metrics_reporter(
      raw_ref<MetricsLibraryInterface>::from_ptr(&metrics_library),
      std::make_unique<FakeGuestFileReader>(std::move(files)));

  for (const auto& [expected_metrics_name, expected_sample_value] : expected) {
    EXPECT_CALL(metrics_library, SendToUMA(expected_metrics_name,
                                           expected_sample_value, _, _, _))
        .Times(1)
        .WillOnce(testing::Return(true));
  }

  metrics_reporter.ReportMetrics(0, name_prefix, disks);
}

TEST_F(VirtioBlkMetricsTest, ReportMetricsIgnoreZeroStatsOfInactiveDisk) {
  StrictMock<MetricsLibraryMock> metrics_library;

  const std::vector<std::string> disks{"vda", "vdb"};
  const Files files{
      {"/sys/block/vda/stat",
       // read_ios, read_merges, read_sectors, read_ticks, write_ios,
       " 420        0            4200          42          420"
       // write_merges, write_sectors, write_ticks, in_flight, io_ticks,
       " 0              4200           42           0          42"
       // time_in_queue, discard_ios, discard_merges, discard_sectors,
       " 420             420          0               4200"
       // discard_ticks, flush_ios, flush_ticks
       " 42              420        42\n"},
      {"/sys/block/vdb/stat",
       // read_ios, read_merges, read_sectors, read_ticks, write_ios,
       " 0          0            0           0          0"
       // write_merges, write_sectors, write_ticks, in_flight, io_ticks,
       " 0          0            0           0          0"
       // time_in_queue, discard_ios, discard_merges, discard_sectors,
       " 0          0            0           0          0"
       // discard_ticks, flush_ios, flush_ticks
       "            0            0\n"},
  };

  const char* name_prefix = "Disk";
  const std::tuple<const char*, int> expected[] = {
      {"Disk.IoTicks", 42},
      {"Disk.IoSize", (4200 * 2) * 512 / 1024 / 1024},
      {"Disk.IoCount", 420 * 4},
      {"Disk.KbPerTicks", (4200 * 2) * 512 / 1024 / 42},
  };

  VirtioBlkMetrics metrics_reporter(
      raw_ref<MetricsLibraryInterface>::from_ptr(&metrics_library),
      std::make_unique<FakeGuestFileReader>(std::move(files)));

  for (const auto& [expected_metrics_name, expected_sample_value] : expected) {
    EXPECT_CALL(metrics_library, SendToUMA(expected_metrics_name,
                                           expected_sample_value, _, _, _))
        .Times(1)
        .WillOnce(testing::Return(true));
  }

  metrics_reporter.ReportMetrics(0, name_prefix, disks);
}

TEST_F(VirtioBlkMetricsTest, ReportMetricsIgnoresNonExistentDisk) {
  StrictMock<MetricsLibraryMock> metrics_library;

  const std::vector<std::string> disks{"vda", "vde"};
  // `files` does not have `vde`.
  const Files files{
      {"/sys/block/vda/stat",
       // read_ios, read_merges, read_sectors, read_ticks, write_ios,
       " 420        0            4200          42          420"
       // write_merges, write_sectors, write_ticks, in_flight, io_ticks,
       " 0              4200           42           0          42"
       // time_in_queue, discard_ios, discard_merges, discard_sectors,
       " 420             420          0               4200"
       // discard_ticks, flush_ios, flush_ticks
       " 42              420        42\n"},
  };
  const char* name_prefix = "Disk";

  VirtioBlkMetrics metrics_reporter(
      raw_ref<MetricsLibraryInterface>::from_ptr(&metrics_library),
      std::make_unique<FakeGuestFileReader>(std::move(files)));

  // We don't care specific values in this test;
  EXPECT_CALL(metrics_library, SendToUMA("Disk.IoTicks", _, _, _, _))
      .Times(1)
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(metrics_library, SendToUMA("Disk.IoCount", _, _, _, _))
      .Times(1)
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(metrics_library, SendToUMA("Disk.IoSize", _, _, _, _))
      .Times(1)
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(metrics_library, SendToUMA("Disk.KbPerTicks", _, _, _, _))
      .Times(1)
      .WillRepeatedly(testing::Return(true));

  metrics_reporter.ReportMetrics(0, name_prefix, disks);
}

TEST_F(VirtioBlkMetricsTest,
       ReportMetricsDoesNotReportInactiveZeroMetricsDisk) {
  StrictMock<MetricsLibraryMock> metrics_library;

  const std::vector<std::string> disks{"vda", "vdb"};
  const Files files{
      {"/sys/block/vda/stat",
       // read_ios, read_merges, read_sectors, read_ticks, write_ios,
       "         0            0             0           0          0"
       // write_merges, write_sectors, write_ticks, in_flight, io_ticks,
       "         0            0             0           0          0"
       // time_in_queue, discard_ios, discard_merges, discard_sectors,
       "         0            0             0           0"
       // discard_ticks, flush_ios, flush_ticks
       "         0            0             0\n"},
  };

  VirtioBlkMetrics metrics_reporter(
      raw_ref<MetricsLibraryInterface>::from_ptr(&metrics_library),
      std::make_unique<FakeGuestFileReader>(std::move(files)));

  // There should not be any report
  EXPECT_CALL(metrics_library, SendToUMA(_, _, _, _, _)).Times(0);

  metrics_reporter.ReportMetrics(0, "", disks);
}

TEST_F(VirtioBlkMetricsTest, ReportMetricsDoesNotReportNoDiskCase) {
  StrictMock<MetricsLibraryMock> metrics_library;

  const std::vector<std::string> disks{"vda"};
  // No file
  const Files files{};

  VirtioBlkMetrics metrics_reporter(
      raw_ref<MetricsLibraryInterface>::from_ptr(&metrics_library),
      std::make_unique<FakeGuestFileReader>(std::move(files)));

  // There should not be any report
  EXPECT_CALL(metrics_library, SendToUMA(_, _, _, _, _)).Times(0);

  metrics_reporter.ReportMetrics(0, "Disk", disks);
}

TEST_F(VirtioBlkMetricsTest, ReportMetricsDoesNotReportInvalidFile) {
  StrictMock<MetricsLibraryMock> metrics_library;

  const std::vector<std::string> disks{"vda"};
  const Files files{{"/sys/block/vda/stat", "Some Invalid File"}};

  VirtioBlkMetrics metrics_reporter(
      raw_ref<MetricsLibraryInterface>::from_ptr(&metrics_library),
      std::make_unique<FakeGuestFileReader>(std::move(files)));

  // There should not be any report
  EXPECT_CALL(metrics_library, SendToUMA(_, _, _, _, _)).Times(0);

  metrics_reporter.ReportMetrics(0, "Disk", disks);
}

TEST_F(VirtioBlkMetricsTest, ReportBootMetricsReportsCorrectDisksAndNames) {
  StrictMock<MetricsLibraryMock> metrics_library;

  const Files files{
      {"/sys/block/vda/stat",
       // read_ios, read_merges, read_sectors, read_ticks, write_ios,
       " 420        0            4200          42          420"
       // write_merges, write_sectors, write_ticks, in_flight, io_ticks,
       " 0              4200           42           0          42"
       // time_in_queue, discard_ios, discard_merges, discard_sectors,
       " 420             420          0               4200"
       // discard_ticks, flush_ios, flush_ticks
       " 42              420        42\n"},
      {"/sys/block/vdb/stat",
       // read_ios, read_merges, read_sectors, read_ticks, write_ios,
       " 100        0            1000          10          100"
       // write_merges, write_sectors, write_ticks, in_flight, io_ticks,
       " 0              1000           10           0          10"
       // time_in_queue, discard_ios, discard_merges, discard_sectors,
       " 100             100          0               1000"
       // discard_ticks, flush_ios, flush_ticks
       " 10              100        10\n"},
      {"/sys/block/vde/stat",
       // read_ios, read_merges, read_sectors, read_ticks, write_ios,
       " 120        0            1200          12          120"
       // write_merges, write_sectors, write_ticks, in_flight, io_ticks,
       " 0              1200           12           0          12"
       // time_in_queue, discard_ios, discard_merges, discard_sectors,
       " 120             120          0               1200"
       // discard_ticks, flush_ios, flush_ticks
       " 12              120        12\n"},
  };

  const std::tuple<const char*, int> expected[] = {
      {"Virtualization.ARCVM.Disk.Boot.IoTicks", 42 + 10 + 12},
      {"Virtualization.ARCVM.Disk.Boot.IoSize",
       static_cast<int>((4200 * 2 + 1000 * 2 + 1200 * 2) * 512 / 1024 / 1024)},
      {"Virtualization.ARCVM.Disk.Boot.IoCount", 420 * 4 + 100 * 4 + 120 * 4},
      {"Virtualization.ARCVM.Disk.Boot.KbPerTicks",
       static_cast<int>((4200 * 2 + 1000 * 2 + 1200 * 2) * 512 / 1024 / 64)},
  };

  VirtioBlkMetrics metrics_reporter(
      raw_ref<MetricsLibraryInterface>::from_ptr(&metrics_library),
      std::make_unique<FakeGuestFileReader>(std::move(files)));

  for (const auto& [expected_metrics_name, expected_sample_value] : expected) {
    EXPECT_CALL(metrics_library, SendToUMA(expected_metrics_name,
                                           expected_sample_value, _, _, _))
        .Times(1)
        .WillOnce(testing::Return(true));
  }

  metrics_reporter.ReportBootMetrics(apps::VmType::ARCVM, 0);
}

TEST_F(VirtioBlkMetricsTest, ReportDeltaMetricsReportsCorrectMetrics) {
  StrictMock<MetricsLibraryMock> metrics_library;

  const std::vector<std::string> disks{"vda"};
  FakeGuestFileReader* file_reader = new FakeGuestFileReader({
      {"/sys/block/vda/stat",
       // read_ios, read_merges, read_sectors, read_ticks, write_ios,
       " 100        0            10000         10          100"
       // write_merges, write_sectors, write_ticks, in_flight, io_ticks,
       " 0              10000          10           0          10"
       // time_in_queue, discard_ios, discard_merges, discard_sectors,
       " 100             100          0               1000"
       // discard_ticks, flush_ios, flush_ticks
       " 10              100        10\n"},
  });

  VirtioBlkMetrics metrics_reporter(
      raw_ref<MetricsLibraryInterface>::from_ptr(&metrics_library),
      std::unique_ptr<VshFileReader>(static_cast<VshFileReader*>(file_reader)));

  const char* name_prefix = "Disk";
  // First, metrics should be calculated based on the file contents
  const std::tuple<const char*, int> expected_first_metrics[] = {
      {"Disk.IoTicks", 10},
      {"Disk.IoSize", (10000 * 2) * 512 / 1024 / 1024},
      {"Disk.IoCount", 100 * 4},
      {"Disk.KbPerTicks", (10000 * 2) * 512 / 1024 / 10},
  };

  for (const auto& [expected_metrics_name, expected_sample_value] :
       expected_first_metrics) {
    EXPECT_CALL(metrics_library, SendToUMA(expected_metrics_name,
                                           expected_sample_value, _, _, _))
        .Times(1)
        .WillOnce(testing::Return(true));
  }

  SysBlockStat previous_block_stat{};
  metrics_reporter.ReportDeltaMetrics(0, name_prefix, disks,
                                      previous_block_stat);

  // previous_block_stat should be updated to the new stat.
  EXPECT_EQ(previous_block_stat[kReadIosIndex], 100);

  // Now, stats increased to 5000, 500, or 50
  file_reader->faked_files["/sys/block/vda/stat"] =
      // read_ios, read_merges, read_sectors, read_ticks, write_ios,
      " 500        0            50000         50          500"
      // write_merges, write_sectors, write_ticks, in_flight, io_ticks,
      " 0              50000          50           0          50"
      // time_in_queue, discard_ios, discard_merges, discard_sectors,
      " 500             500          0               5000"
      // discard_ticks, flush_ios, flush_ticks
      " 50              500        50\n";

  const std::tuple<const char*, int> expected_second_metrics[] = {
      {"Disk.IoTicks", 50 - 10},
      {"Disk.IoSize", (50000 * 2 - 10000 * 2) * 512 / 1024 / 1024},
      {"Disk.IoCount", 500 * 4 - 100 * 4},
      {"Disk.KbPerTicks", (50000 * 2 - 10000 * 2) * 512 / 1024 / 40},
  };

  for (const auto& [expected_metrics_name, expected_sample_value] :
       expected_second_metrics) {
    EXPECT_CALL(metrics_library, SendToUMA(expected_metrics_name,
                                           expected_sample_value, _, _, _))
        .Times(1)
        .WillOnce(testing::Return(true));
  }

  metrics_reporter.ReportDeltaMetrics(0, name_prefix, disks,
                                      previous_block_stat);
}

TEST_F(VirtioBlkMetricsTest,
       ScheduleDailyMetricsReportsCorrectDisksAndMetrics) {
  StrictMock<MetricsLibraryMock> metrics_library;
  base::MockRepeatingTimer* timer = new base::MockRepeatingTimer;
  FakeGuestFileReader* file_reader = new FakeGuestFileReader({
      {"/sys/block/vda/stat",
       // read_ios, read_merges, read_sectors, read_ticks, write_ios,
       " 100        0            1000          10          100"
       // write_merges, write_sectors, write_ticks, in_flight, io_ticks,
       " 0              1000           10           0          10"
       // time_in_queue, discard_ios, discard_merges, discard_sectors,
       " 100             100          0               1000"
       // discard_ticks, flush_ios, flush_ticks
       " 10              100        10\n"},
      {"/sys/block/vdb/stat",
       // read_ios, read_merges, read_sectors, read_ticks, write_ios,
       " 100        0            1000          10          100"
       // write_merges, write_sectors, write_ticks, in_flight, io_ticks,
       " 0              1000           10           0          10"
       // time_in_queue, discard_ios, discard_merges, discard_sectors,
       " 100             100          0               1000"
       // discard_ticks, flush_ios, flush_ticks
       " 10              100        10\n"},
      {"/sys/block/vde/stat",
       // read_ios, read_merges, read_sectors, read_ticks, write_ios,
       " 100        0            1000          10          100"
       // write_merges, write_sectors, write_ticks, in_flight, io_ticks,
       " 0              1000           10           0          10"
       // time_in_queue, discard_ios, discard_merges, discard_sectors,
       " 100             100          0               1000"
       // discard_ticks, flush_ios, flush_ticks
       " 10              100        10\n"},
  });

  VirtioBlkMetrics metrics_reporter(
      raw_ref<MetricsLibraryInterface>::from_ptr(&metrics_library),
      std::unique_ptr<VshFileReader>(static_cast<VshFileReader*>(file_reader)),
      std::unique_ptr<base::RepeatingTimer>(
          static_cast<base::RepeatingTimer*>(timer)));

  // First, metrics should be calculated based on the file contents
  const std::tuple<const char*, int> expected_first_metrics[] = {
      {"Virtualization.ARCVM.Disk.Daily.IoTicks", 10 * 3},
      {"Virtualization.ARCVM.Disk.Daily.IoSize",
       (1000 * 2 * 3) * 512 / 1024 / 1024},
      {"Virtualization.ARCVM.Disk.Daily.IoCount", 100 * 3 * 4},
      {"Virtualization.ARCVM.Disk.Daily.KbPerTicks",
       (1000 * 2 * 3) * 512 / 1024 / 30},
  };

  for (const auto& [expected_metrics_name, expected_sample_value] :
       expected_first_metrics) {
    EXPECT_CALL(metrics_library, SendToUMA(expected_metrics_name,
                                           expected_sample_value, _, _, _))
        .Times(1)
        .WillOnce(testing::Return(true));
  }

  metrics_reporter.ScheduleDailyMetrics(apps::VmType::ARCVM, 0);

  // Fire the first report
  timer->Fire();

  // Now, stats of vda increased by 4000, 400, or 40.
  file_reader->faked_files["/sys/block/vda/stat"] =
      // read_ios, read_merges, read_sectors, read_ticks, write_ios,
      " 500        0            5000          50          500"
      // write_merges, write_sectors, write_ticks, in_flight, io_ticks,
      " 0              5000           50           0          50"
      // time_in_queue, discard_ios, discard_merges, discard_sectors,
      " 500             500          0               5000"
      // discard_ticks, flush_ios, flush_ticks
      " 50              500        50\n";

  const std::tuple<const char*, int> expected_second_metrics[] = {
      // Increased by 40
      {"Virtualization.ARCVM.Disk.Daily.IoTicks", 40},
      {"Virtualization.ARCVM.Disk.Daily.IoSize",
       (4000 * 2) * 512 / 1024 / 1024},
      {"Virtualization.ARCVM.Disk.Daily.IoCount", 400 * 4},
      {"Virtualization.ARCVM.Disk.Daily.KbPerTicks",
       (4000 * 2) * 512 / 1024 / 40},
  };

  for (const auto& [expected_metrics_name, expected_sample_value] :
       expected_second_metrics) {
    EXPECT_CALL(metrics_library, SendToUMA(expected_metrics_name,
                                           expected_sample_value, _, _, _))
        .Times(1)
        .WillOnce(testing::Return(true));
  }

  // Fire the second report
  timer->Fire();
}

}  // namespace vm_tools::concierge
