// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VIRTIO_BLK_METRICS_H_
#define VM_TOOLS_CONCIERGE_VIRTIO_BLK_METRICS_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/sequence_checker.h>
#include <base/strings/string_piece.h>
#include <base/timer/timer.h>
#include <metrics/metrics_library.h>
#include <vm_applications/apps.pb.h>

#include "vm_tools/common/vm_id.h"

namespace vm_tools::concierge {

// Reads a file from a guest VM by cat via vsh.
// Methods are virtual to make them fake-able.
class VshFileReader {
 public:
  // Reads a file at `file_path` from a guest of `cid`.
  virtual std::optional<std::string> Read(uint32_t cid,
                                          const std::string& file_path) const;
  // Checks if a regular file at `file_path` exists in a guest of `cid`
  virtual std::optional<bool> CheckIfExists(uint32_t cid,
                                            const std::string& file_path) const;
  virtual ~VshFileReader() = default;
};

// Represents indices of stat values you can read from `/sys/block/*/stat`.
enum SysBlockStatIndex {
  // Number of read I/Os
  kReadIosIndex = 0,
  // Number of sectors read
  kReadSectorsIndex = 2,
  // Number of write I/Os
  kWriteIosIndex = 4,
  // Number of sectors written
  kWriteSectorsIndex = 6,
  // Total active time of the block device in milliseconds. The actual max size
  // of this metrics is int32_t, but uint64_t is used for easier parse.
  kIoTicksIndex = 9,
  // Number of discard IOs
  kDiscardIosIndex = 11,
  // Number of flush IOs
  kFlushIosIndex = 15,
  kMaxSysBlockStatIndex = 17,
};

// Represents stat values you can read from `/sys/block/*/stat`. Defined as an
// array instead of a struct, since we iterate on them a lot
using SysBlockStat = std::array<uint64_t, kMaxSysBlockStatIndex>;

// Sends virtio-blk related metrics to UMA.
//
// Calculates block device metrics by reading the guest stat files like
// `/sys/block/vda/stat` via vsh. Currently, VirtioBlkMetrics supports only
// ArcVM, but it should be able to collect metrics from other VMs.
class VirtioBlkMetrics {
 public:
  // The constructor.
  explicit VirtioBlkMetrics(
      base::raw_ref<MetricsLibraryInterface> metrics_library,
      std::unique_ptr<VshFileReader> vhs_file_reader =
          std::make_unique<VshFileReader>(),
      std::unique_ptr<base::RepeatingTimer> daily_report_timer =
          std::make_unique<base::RepeatingTimer>())
      : metrics_library_(metrics_library),
        vsh_file_reader_(std::move(vhs_file_reader)),
        daily_report_timer_(std::move(daily_report_timer)) {}

  // Calculates and sends virtio-blk metrics of the guest with `cid`. `disks`
  // is a vector of the the file name of the block device in the guest like
  // `vda`.
  void ReportMetrics(uint32_t cid,
                     const std::string& metrics_category_name,
                     const std::vector<std::string>& disks) const;

  // Calculates and sends the delta virtio-blk metrics of the guest with `cid`
  // from the metrics of `previous_block_stat`. `ReportDeltaMetrics` also
  // updates the given `previous_block_stat` with the new stats.
  void ReportDeltaMetrics(uint32_t cid,
                          const std::string& metrics_category_name,
                          const std::vector<std::string>& disks,
                          SysBlockStat& previous_block_stat) const;

  // Report Virtio-blk metrics on a VM boot.
  void ReportBootMetrics(apps::VmType vm_type, uint32_t cid) const;

  // Schedule the daily report of the virtio-blk metrics.
  void ScheduleDailyMetrics(apps::VmType vm_type, uint32_t cid);

 private:
  // Stores a pointer to the metrics library instance
  const base::raw_ref<MetricsLibraryInterface> metrics_library_
      GUARDED_BY_CONTEXT(sequence_checker_);
  // Stores a guest file reader
  const std::unique_ptr<VshFileReader> vsh_file_reader_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Timer which fires for the daily report
  std::unique_ptr<base::RepeatingTimer> daily_report_timer_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Stores the SysBlockStat retrieved in the previous daily ArcVM report.
  SysBlockStat previous_block_stat_ GUARDED_BY_CONTEXT(sequence_checker_){};

  // Ensure calls are made on the right thread.
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_VIRTIO_BLK_METRICS_H_
