// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_MM_BALLOON_METRICS_H_
#define VM_TOOLS_CONCIERGE_MM_BALLOON_METRICS_H_

#include <string>

#include <base/functional/callback.h>
#include <base/sequence_checker.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include <vm_applications/apps.pb.h>

#include "vm_tools/concierge/mm/balloon.h"

namespace vm_tools::concierge::mm {

class BalloonMetrics {
 public:
  BalloonMetrics(apps::VmType vm_type,
                 const raw_ref<MetricsLibraryInterface> metrics,
                 base::RepeatingCallback<base::TimeTicks(void)> time_ticks_now =
                     base::BindRepeating(&base::TimeTicks::Now));
  ~BalloonMetrics();

  // Not copyable or movable
  BalloonMetrics(const BalloonMetrics&) = delete;
  BalloonMetrics& operator=(const BalloonMetrics&) = delete;

  void OnResize(Balloon::ResizeResult result);

  void OnStall(Balloon::StallStatistics stats);

  apps::VmType GetVmType() const;

 private:
  void LogSizeIfNeeded(int size_mib, base::TimeTicks now);

  // Ensure calls are made on the right sequence.
  SEQUENCE_CHECKER(sequence_checker_);

  // What kind of VM this balloon is for.
  const apps::VmType vm_type_ GUARDED_BY_CONTEXT(sequence_checker_);

  // Metrics logging helpers.
  const raw_ref<MetricsLibraryInterface> metrics_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Getter for TimeTicks::Now
  const base::RepeatingCallback<base::TimeTicks(void)> time_ticks_now_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // The time of the previous resize, or startup if there hasn't been one yet.
  base::TimeTicks resize_interval_start_ GUARDED_BY_CONTEXT(sequence_checker_);

  // The last effective time we logged the absolute balloon size, or startup
  // if we haven't. Back-dated to the last integer multiple of
  // kSizeMetricInterval after startup time.
  base::TimeTicks last_size_log_time_ GUARDED_BY_CONTEXT(sequence_checker_);

  // The most recently logged size of the balloon. Used to log any remaining
  // size samples at shutdown.
  int last_size_mib_ GUARDED_BY_CONTEXT(sequence_checker_);
};

}  // namespace vm_tools::concierge::mm

#endif  // VM_TOOLS_CONCIERGE_MM_BALLOON_METRICS_H_
