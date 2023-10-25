// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VMM_SWAP_TBW_POLICY_H_
#define VM_TOOLS_CONCIERGE_VMM_SWAP_TBW_POLICY_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/containers/ring_buffer.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/sequence_checker.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <base/types/expected.h>
#include <metrics/metrics_library.h>

#include "vm_concierge/vmm_swap_policy.pb.h"
#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/vmm_swap_history_file_manager.h"

namespace vm_tools::concierge {

// VmmSwapTbwPolicy tracks the TBW (Total Bytes Written) from vmm-swap feature
// and decides whether it is able to swap out or not based on 28 days history
// not to exceeds the given target.
//
// Managing TBW is important because because swapping out too many memory into
// the swap file damages the disk.
//
// VmmSwapTbwPolicy persistes the history to the file specified by `Init()`.
// The file content is serialized by `TbwHistoryEntry` protobuf message.
//
// If the file does not exists, the policy creates the history file and
// initializing it pessimistically as if there were full target TBW through last
// 28 days. If any file related operation fails, VmmSwapTbwPolicy deletes the
// history file and stop writing to the file after that. When the concierge
// restarts, the policy restart from a pessimistic history.
//
// VmmSwapTbwPolicy rotates the history file before the file size reaches to
// 4096 bytes. During rotating, the policy creates another history file which
// has ".tmp" suffix to the original history file name temporarily and replace
// the original file with the new file.
//
// VmmSwapTbwPolicy reports the total bytes written to UMA weekly as
// "Memory.VmmSwap.TotalBytesWrittenInAWeek" once any disk writes for vmm-swap
// has done. Once reporting has started it reports weekly even if the total
// bytes written is zero.
//
// VmmSwapTbwPolicy is not thread-safe.
class VmmSwapTbwPolicy final {
 public:
  explicit VmmSwapTbwPolicy(const raw_ref<MetricsLibraryInterface> metrics,
                            const base::FilePath history_file_path,
                            std::unique_ptr<base::RepeatingTimer> report_timer =
                                std::make_unique<base::RepeatingTimer>());
  VmmSwapTbwPolicy(const VmmSwapTbwPolicy&) = delete;
  VmmSwapTbwPolicy& operator=(const VmmSwapTbwPolicy&) = delete;
  ~VmmSwapTbwPolicy() = default;

  // Set the target tbw per day.
  void SetTargetTbwPerDay(uint64_t target_tbw_per_day);
  // Get the target tbw per day.
  uint64_t GetTargetTbwPerDay();

  // Restore the tbw history from the history file.
  //
  // This creates the file if it does not exist.
  //
  // The `time` is injectable for testing purpose.
  bool Init(base::Time time = base::Time::Now());

  // Record a tbw history entry.
  //
  // The given `time` is expected to be later than previous Record() calls.
  // The `time` is injectable for testing purpose.
  void Record(uint64_t bytes_written, base::Time time = base::Time::Now());

  // Returns whether it is able to vmm-swap out the guest memory in terms of
  // TBW.
  //
  // The `time` is injectable for testing purpose.
  bool CanSwapOut(base::Time time = base::Time::Now()) const;

  // Each repeated message has 1 byte tag & length varint prepended. The length
  // varint is 1 byte because TbwHistoryEntry is at most 22 bytes.
  // TbwHistoryEntry has at most 22 (1+10 [tag+uint64] + 1+10 [tag+int64])
  // bytes/message.
  static constexpr int kMaxEntrySize = 24;

 private:
  struct BytesWritten {
    base::Time started_at;
    uint64_t size;
  };
  static constexpr size_t kTbwHistoryLength = 28;
  // 1 page size is the max file size.
  static constexpr size_t kMaxFileSize = KiB(4);
  // The file can contain more than kTbwHistoryLength entries.
  static_assert(kMaxEntrySize * kTbwHistoryLength < kMaxFileSize,
                "The tbw history file does not have enough size to hold "
                "kTbwHistoryLength entries");

  const raw_ref<MetricsLibraryInterface> metrics_
      GUARDED_BY_CONTEXT(sequence_checker_);
  uint64_t target_tbw_per_day_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;
  base::RingBuffer<BytesWritten, kTbwHistoryLength> tbw_history_
      GUARDED_BY_CONTEXT(sequence_checker_);
  const VmmSwapHistoryFileManager history_file_path_;
  std::optional<base::File> history_file_ GUARDED_BY_CONTEXT(sequence_checker_);
  std::optional<base::Time> last_reported_at_
      GUARDED_BY_CONTEXT(sequence_checker_);
  std::unique_ptr<base::RepeatingTimer> report_timer_
      GUARDED_BY_CONTEXT(sequence_checker_);

  bool TryRotateFile(base::Time time);
  bool WriteEntry(TbwHistoryEntry entry, base::Time time, bool try_rotate);
  // Write tbw entry to the history file.
  //
  // If the file is not present, this do nothing.
  // This rotates the file if the file size may exceeds the max file size.
  // This deletes the file if it fails to rotate the file or to write an entry.
  //
  // This returns false in either of cases when:
  //
  // * The file is already deleted,
  // * It fails to rotate the file, or
  // * It fails to write an entry.
  bool WriteBytesWrittenEntry(uint64_t bytes_written,
                              base::Time time,
                              bool try_rotate = true);
  // Write report fence entry to the history file.
  //
  // Behaves the similar to |WriteBytesWrittenEntry|.
  bool WriteReportEntry(base::Time time, bool try_rotate = true);
  bool LoadFromFile(base::File& file, base::Time now);
  void AppendEntry(uint64_t bytes_written, base::Time time);
  bool RotateHistoryFile(base::Time time);
  void DeleteFile();
  void MarkReported(base::Time time);
  void ReportTbwOfWeek();

  // Ensure calls are made on the right thread.
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_VMM_SWAP_TBW_POLICY_H_
