// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_METRICS_DURATION_RECORDER_H_
#define VM_TOOLS_CONCIERGE_METRICS_DURATION_RECORDER_H_

#include <string>

#include <base/thread_annotations.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>
#include <vm_applications/apps.pb.h>

namespace vm_tools::concierge::metrics {

// This class is used to calculate and report the duration of its scope on
// destruction.
class DurationRecorder {
 public:
  // Events that can be logged.
  enum Event {
    kVmStart = 1,
    kVmStop,
  };

  DurationRecorder(const raw_ref<MetricsLibraryInterface> metrics,
                   apps::VmType vm_type,
                   Event event);

  ~DurationRecorder();

 private:
  SEQUENCE_CHECKER(sequence_checker_);

  // The time when this object is instantiated.
  base::TimeTicks start_time_ GUARDED_BY_CONTEXT(sequence_checker_);

  // The type of the VM associated with this object.
  apps::VmType vm_type_ GUARDED_BY_CONTEXT(sequence_checker_);

  // The event associated with this object.
  Event event_ GUARDED_BY_CONTEXT(sequence_checker_);

  // Used to log the metrics.
  const raw_ref<MetricsLibraryInterface> metrics_
      GUARDED_BY_CONTEXT(sequence_checker_);

  DurationRecorder(const DurationRecorder&) = delete;
  DurationRecorder& operator=(const DurationRecorder&) = delete;
};

namespace internal {

// Returns the name of a metric given a VM's type and the event.
std::string GetVirtualizationMetricsName(apps::VmType vm_type,
                                         DurationRecorder::Event event);

}  // namespace internal

}  // namespace vm_tools::concierge::metrics

#endif  // VM_TOOLS_CONCIERGE_METRICS_DURATION_RECORDER_H_
