// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/metrics/duration_recorder.h"

#include <string>

#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/notreached.h>
#include <base/sequence_checker.h>
#include <base/strings/strcat.h>
#include <base/time/time.h>
#include <vm_applications/apps.pb.h>

namespace vm_tools::concierge::metrics {

namespace {

// Constants related to logging Vm Start and Vm Stop times.
constexpr char kVmStartMetricsTag[] = "Start";
constexpr char kDurationSuffix[] = "Duration";
// Modify this as per the max timeout here -
// https://source.chromium.org/chromiumos/chromiumos/codesearch/+/main:src/platform2/vm_tools/init/vm_concierge.conf;l=46?q=file:vm_concierge.conf.
// Currently, we choose a value slightly higher than that timeout.
constexpr base::TimeDelta kMaxDuration = base::Seconds(50);
constexpr int kMetricsBuckets = 50;

}  // namespace

namespace internal {

// Returns the name of a metric given a VM's type and the event.
std::string GetVirtualizationMetricsName(apps::VmType vm_type,
                                         DurationRecorder::Event event) {
  std::string event_name;
  switch (event) {
    case DurationRecorder::Event::kVmStart:
      event_name = kVmStartMetricsTag;
      break;
    default:
      NOTREACHED();
      LOG(ERROR) << "Unknown vm event for MetricsInstrumenter: " << event_name;
      // Unknown key for UMA are just ignored.
      event_name = "UnknownEvent";
      break;
  }

  // This will create names such as "crostini.VmStart.Duration" or
  // "borealis.VmStop.duration". The VMs already have buckets registered for
  // them.
  return base::StrCat({"Virtualization", ".", apps::VmType_Name(vm_type), ".",
                       event_name, ".", kDurationSuffix});
}

}  // namespace internal

DurationRecorder::DurationRecorder(
    const raw_ref<MetricsLibraryInterface> metrics,
    apps::VmType vm_type,
    Event event)
    : vm_type_(vm_type), event_(event), metrics_(metrics) {
  start_time_ = base::TimeTicks::Now();
}

DurationRecorder::~DurationRecorder() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::TimeDelta duration = base::TimeTicks::Now() - start_time_;
  metrics_->SendTimeToUMA(
      internal::GetVirtualizationMetricsName(vm_type_, event_), duration,
      base::TimeDelta(), kMaxDuration, kMetricsBuckets);
}

}  // namespace vm_tools::concierge::metrics
