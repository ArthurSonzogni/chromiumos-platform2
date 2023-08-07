// // Copyright 2023 The ChromiumOS Authors
// // Use of this source code is governed by a BSD-style license that can be
// // found in the LICENSE file.

#include "oobe_config/metrics/structured_metrics_recorder.h"

#include <base/logging.h>
#include <base/notreached.h>
#include <metrics/structured_events.h>

namespace oobe_config {

void RecordStructuredMetric(const EventData& event_data,
                            const RollbackMetadata& rollback_metadata) {
  // TODO(b/261850979): Report all events using structured metrics.
  switch (event_data.event()) {
    case EnterpriseRollbackEvent::ROLLBACK_POLICY_ACTIVATED:
      StructuredMetricRollbackPolicyActivated(rollback_metadata);
      break;
    case EnterpriseRollbackEvent::EVENT_UNSPECIFIED:
      LOG(INFO) << "Event to record undefined.";
      break;
    default:
      // Recording is called in the target version. The default case is
      // expected when the proto does not support the event tracked in a
      // newest version yet. This is not an error but the newest metric
      // event will not be recorded.
      LOG(WARNING) << "Event to record not supported yet.";
      break;
  }
}

void StructuredMetricRollbackPolicyActivated(
    const RollbackMetadata& rollback_metadata) {
  LOG(INFO) << "Record RollbackPolicyActivated event.";
  metrics::structured::events::rollback_enterprise::RollbackPolicyActivated()
      .Setorigin_chromeos_version_major(
          rollback_metadata.origin_chromeos_version_major())
      .Setorigin_chromeos_version_minor(
          rollback_metadata.origin_chromeos_version_minor())
      .Setorigin_chromeos_version_patch(
          rollback_metadata.origin_chromeos_version_patch())
      .Settarget_chromeos_version_major(
          rollback_metadata.target_chromeos_version_major())
      .Settarget_chromeos_version_minor(
          rollback_metadata.target_chromeos_version_minor())
      .Settarget_chromeos_version_patch(
          rollback_metadata.target_chromeos_version_patch())
      .Record();
}

}  // namespace oobe_config
