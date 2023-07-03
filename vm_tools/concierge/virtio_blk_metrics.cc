// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/virtio_blk_metrics.h"

#include <unistd.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <base/allocator/partition_allocator/pointers/raw_ref.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <brillo/process/process.h>
#include <metrics/metrics_library.h>
#include <vm_applications/apps.pb.h>
#include <vm_protos/proto_bindings/vm_guest.pb.h>

namespace vm_tools::concierge {

namespace {

// Block devices which we want to send metrics of.
constexpr std::array<std::string_view, 3> kArcVmDisks = {
    // system
    "vda",
    // vendor
    "vdb",
    // data
    "vde"};

constexpr int kSectorSize = 512;
constexpr char kSysBlockPath[] = "/sys/block";

// Parses `/sys/block/*/stat` file, which contains numbers separated by spaces
// in one line. See https://www.kernel.org/doc/html/next/block/stat.html.
std::optional<SysBlockStat> ParseSysBlockStat(const std::string_view& stat) {
  auto stat_values =
      base::SplitStringPiece(stat, base::kWhitespaceASCII,
                             base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (stat_values.size() < kMaxSysBlockStatIndex) {
    LOG(ERROR) << "Not enough items in a block stat: " << stat;
    return std::nullopt;
  }

  SysBlockStat sys_block_stat;
  for (int i = 0; i < sys_block_stat.size(); i++) {
    if (!base::StringToUint64(stat_values[i], &sys_block_stat[i])) {
      LOG(ERROR) << "Failed to parse the " << i << "th item: " << stat;
      return std::nullopt;
    }
  }

  return sys_block_stat;
}

// Put SysBlockStats for the given `disks` to the given `block_stats`, skipping
// non-existent disks.
std::optional<std::vector<SysBlockStat>> GetSysBlockStats(
    uint32_t cid,
    const std::vector<std::string>& disks,
    VshFileReader& guest_file_reader) {
  std::vector<SysBlockStat> sys_block_stats;
  for (const auto& disk : disks) {
    const auto stat_path = base::JoinString({kSysBlockPath, disk, "stat"}, "/");

    const std::optional<bool> block_exists =
        guest_file_reader.CheckIfExists(cid, stat_path);
    if (!block_exists.has_value()) {
      LOG(ERROR) << "Failed to check if the block stat file exists";
      return std::nullopt;
    }
    if (!block_exists.value()) {
      // The block disk does not exist on this device. Skip it.
      continue;
    }

    const std::optional<std::string> stat_str =
        guest_file_reader.Read(cid, stat_path);
    if (!stat_str) {
      LOG(ERROR) << "Failed to read " << stat_path;
      return std::nullopt;
    }
    const std::optional<SysBlockStat> sys_block_stat =
        ParseSysBlockStat(stat_str.value());
    if (!sys_block_stat) {
      LOG(ERROR) << "Failed to parse " << stat_path;
      return std::nullopt;
    }
    sys_block_stats.push_back(std::move(sys_block_stat.value()));
  }

  return sys_block_stats;
}

void MakeVsh(brillo::ProcessImpl& process,
             uint32_t cid,
             const std::vector<const std::string>& args) {
  process.AddArg("/usr/bin/vsh");
  process.AddArg(base::StringPrintf("--cid=%u", cid));
  process.AddArg("--user=root");
  process.AddArg("--");
  process.RedirectUsingMemory(STDOUT_FILENO);
  process.RedirectUsingMemory(STDERR_FILENO);
  for (const auto& arg : args) {
    process.AddArg(arg);
  }
}

void SendMetricToUma(
    const int value,
    const std::string& metrics_category_name,
    const std::string& metrics_name,
    const int max_value,
    const int bucket_count,
    const base::raw_ref<MetricsLibraryInterface> metrics_library) {
  const auto full_metrics_name =
      base::JoinString({metrics_category_name, metrics_name}, ".");

  if (!metrics_library->SendToUMA(full_metrics_name, value, 1, max_value,
                                  bucket_count)) {
    LOG(ERROR) << "Failed to SendToUma: " << metrics_name;
  }
}

void SendBlockMetricsToUma(
    const SysBlockStat& block_stat,
    const std::string& uma_category_name,
    const base::raw_ref<MetricsLibraryInterface> metrics_library) {
  if (block_stat[kIoTicksIndex] == 0) {
    // There's no disk activities. No metrics to report.
    return;
  }

  // Calculate the metrics
  // See go/virtio-blk-uma for the rationales of max and bucket values.

  // Unit: ms
  const int io_ticks = static_cast<int>(block_stat[kIoTicksIndex]);
  SendMetricToUma(io_ticks, uma_category_name, "IoTicks", 100'000'000, 50,
                  metrics_library);

  // Unit: #
  const int io_count = static_cast<int>(
      block_stat[kReadIosIndex] + block_stat[kWriteIosIndex] +
      block_stat[kFlushIosIndex] + block_stat[kDiscardIosIndex]);
  SendMetricToUma(io_count, uma_category_name, "IoCount", 10'000'000, 50,
                  metrics_library);

  // Unit: Megabytes
  // Calculate in double to obviously show that there's no overflow
  const double io_size = static_cast<double>(block_stat[kReadSectorsIndex] +
                                             block_stat[kWriteSectorsIndex]) *
                         kSectorSize;
  SendMetricToUma(static_cast<int>(io_size / 1024 / 1024), uma_category_name,
                  "IoSize", 1'000'000, 50, metrics_library);

  // Unit: Kilobytes/ms
  const int kb_per_ticks = static_cast<int>(
      io_size / 1024 / static_cast<double>(block_stat[kIoTicksIndex]));
  SendMetricToUma(kb_per_ticks, uma_category_name, "KbPerTicks", 10'000'000, 50,
                  metrics_library);

  return;
}

std::vector<std::string> GetDisksToReport(apps::VmType vm_type) {
  switch (vm_type) {
    case apps::VmType::ARCVM:
      return std::vector<std::string>(kArcVmDisks.begin(), kArcVmDisks.end());
    default:
      return {};
  }
}

std::string GetMetricsCategoryName(apps::VmType vm_type,
                                   const std::string& subcategory) {
  return base::JoinString(
      {"Virtualization", apps::VmType_Name(vm_type), "Disk", subcategory}, ".");
}

}  // namespace

std::optional<bool> VshFileReader::CheckIfExists(
    uint32_t cid, const std::string& path) const {
  brillo::ProcessImpl test;
  MakeVsh(test, cid, {"test", "-f", path});
  if (test.Run() == 0) {
    return true;
  }
  const std::string stderr = test.GetOutputString(STDERR_FILENO);
  if (!stderr.empty()) {
    LOG(ERROR) << "Failed to check if a file exists in the guest. stderr: "
               << stderr;
    return std::nullopt;
  }
  return false;
}

std::optional<std::string> VshFileReader::Read(uint32_t cid,
                                               const std::string& path) const {
  brillo::ProcessImpl cat;
  MakeVsh(cat, cid, {"cat", path});
  if (cat.Run() != 0) {
    LOG(ERROR) << "Failed read a file via vsh. stderr: "
               << cat.GetOutputString(STDERR_FILENO);
    return std::nullopt;
  }

  return cat.GetOutputString(STDOUT_FILENO);
}

void VirtioBlkMetrics::ReportMetrics(
    uint32_t cid,
    const std::string& metrics_category_name,
    const std::vector<std::string>& disks) const {
  SysBlockStat zero_stat{};
  ReportDeltaMetrics(cid, metrics_category_name, disks, zero_stat);
}

void VirtioBlkMetrics::ReportDeltaMetrics(
    uint32_t cid,
    const std::string& metrics_category_name,
    const std::vector<std::string>& disks,
    SysBlockStat& previous_block_stat) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const std::optional<std::vector<SysBlockStat>> block_stats =
      GetSysBlockStats(cid, disks, *vsh_file_reader_);
  if (!block_stats) {
    LOG(ERROR) << "Cannot get block stats";
    return;
  }

  // Accumulate the block stats since the metrics are calculated for the total
  SysBlockStat total_stat{};
  for (const auto& block_stat : block_stats.value()) {
    for (int i = 0; i < total_stat.size(); i++) {
      total_stat[i] += block_stat[i];
    }
  }

  for (int i = 0; i < total_stat.size(); i++) {
    total_stat[i] -= previous_block_stat[i];
    previous_block_stat[i] = total_stat[i];
  }

  SendBlockMetricsToUma(total_stat, metrics_category_name, metrics_library_);
}

void VirtioBlkMetrics::ReportBootMetrics(apps::VmType vm_type,
                                         uint32_t cid) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const std::string metrics_category = GetMetricsCategoryName(vm_type, "Boot");
  const std::vector<std::string> arcvm_blocks = GetDisksToReport(vm_type);

  ReportMetrics(cid, metrics_category, arcvm_blocks);
}

void VirtioBlkMetrics::ScheduleDailyMetrics(apps::VmType vm_type,
                                            uint32_t cid) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  const std::string metrics_category = GetMetricsCategoryName(vm_type, "Daily");
  const std::vector<std::string> arcvm_blocks = GetDisksToReport(vm_type);

  daily_report_timer_->Start(
      FROM_HERE, base::Days(1),
      base::BindRepeating(&VirtioBlkMetrics::ReportDeltaMetrics,
                          base::Unretained(this), cid, metrics_category,
                          arcvm_blocks, std::ref(previous_block_stat_)));
}

}  // namespace vm_tools::concierge
