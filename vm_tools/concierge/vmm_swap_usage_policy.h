// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VMM_SWAP_USAGE_POLICY_H_
#define VM_TOOLS_CONCIERGE_VMM_SWAP_USAGE_POLICY_H_

#include <optional>
#include <string>
#include <utility>

#include <base/containers/ring_buffer.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/sequence_checker.h>
#include <base/time/time.h>
#include <base/types/expected.h>

#include "vm_concierge/vmm_swap_policy.pb.h"
#include "vm_tools/concierge/byte_unit.h"
#include "vm_tools/concierge/vmm_swap_history_file_manager.h"

namespace vm_tools::concierge {

// Predicts the time when vmm-swap will be disabled using last 4 weeks of
// history.
//
// If it can estimate that vmm-swap will be disabled soon, it is not worth to
// enable vmm-swap so that we can save the TBW (total bytes written).
//
// The vmm-swap is enabled when no application exists on ARCVM and disabled when
// the user launches an application. Enable/disable events should have patterns
// from the user's weekly behavior and be predictable.
//
// The policy projects the the vmm-swap usage patterns from each of the previous
// four weeks onto the current week and calculates how long swap would be
// disabled in each case. The final predicted value is the average of those
// calculated values.
class VmmSwapUsagePolicy final {
 public:
  explicit VmmSwapUsagePolicy(base::FilePath history_file_path);
  VmmSwapUsagePolicy(const VmmSwapUsagePolicy&) = delete;
  VmmSwapUsagePolicy& operator=(const VmmSwapUsagePolicy&) = delete;
  ~VmmSwapUsagePolicy() = default;

  bool Init(base::Time time = base::Time::Now());
  void OnEnabled(base::Time time = base::Time::Now());
  void OnDisabled(base::Time time = base::Time::Now());
  void OnDestroy(base::Time time = base::Time::Now());
  // Predict when vmm-swap will be disabled.
  //
  // This returns the duration from the now. The parameter `now` is injectable
  // for mainly testing purpose.
  base::TimeDelta PredictDuration(base::Time now = base::Time::Now());

  // Each repeated message has 1 byte tag & length varint prepended. The length
  // varint is 1 byte because UsageHistoryEntry is at most 24 bytes.
  // UsageHistoryEntry has at most 24 (1+10 [tag+int64] + 1+10 [tag+int64] + 1+1
  // [tag+bool]) bytes/message.
  static constexpr int kMaxEntrySize = 26;

 private:
  struct SwapPeriod {
    base::Time start;
    std::optional<base::TimeDelta> duration;
  };
  static constexpr int kUsageHistoryNumWeeks = 4;
  // The length of the history ring buffer. The history is hourly and at most 4
  // weeks (24 hours * 7 days * 4 weeks).
  static constexpr size_t kUsageHistoryLength = 24 * 7 * kUsageHistoryNumWeeks;
  // 5 page size is the max file size.
  static constexpr size_t kMaxFileSize = 5 * KiB(4);
  // The file can contain more than kUsageHistoryLength entries.
  static_assert(kMaxEntrySize * kUsageHistoryLength < kMaxFileSize,
                "The usage history file does not have enough size to hold "
                "kUsageHistoryLength entries");

  base::RingBuffer<SwapPeriod, kUsageHistoryLength> usage_history_
      GUARDED_BY_CONTEXT(sequence_checker_);
  bool is_enabled_ GUARDED_BY_CONTEXT(sequence_checker_) = false;
  const VmmSwapHistoryFileManager history_file_path_;
  std::optional<base::File> history_file_ GUARDED_BY_CONTEXT(sequence_checker_);

  void AddEnableRecordIfMissing(base::Time time);
  bool TryRotateFile(base::Time time);
  bool WriteEntry(UsageHistoryEntry entry, base::Time time, bool try_rotate);
  // Write enabled duration entry to the history file.
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
  bool WriteEnabledDurationEntry(base::Time time,
                                 base::TimeDelta duration,
                                 bool try_rotate = true);
  // Write shutdown entry to the history file.
  //
  // Behaves the similar to |WriteEnabledDurationEntry|.
  bool WriteShutdownEntry(base::Time time);
  bool LoadFromFile(base::File& file, base::Time now);
  bool RotateHistoryFile(base::Time time);
  void DeleteFile();

  // Ensure calls are made on the right thread.
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_VMM_SWAP_USAGE_POLICY_H_
