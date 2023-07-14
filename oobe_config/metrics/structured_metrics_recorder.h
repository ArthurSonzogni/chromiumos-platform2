// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_METRICS_STRUCTURED_METRICS_RECORDER_H_
#define OOBE_CONFIG_METRICS_STRUCTURED_METRICS_RECORDER_H_

#include <brillo/brillo_export.h>

#include "oobe_config/metrics/enterprise_rollback_metrics_data.pb.h"

namespace oobe_config {

// Ensures the right structured metric is recorded per event.
void BRILLO_EXPORT RecordStructuredMetric(
    const EventData& event_data, const RollbackMetadata& rollback_metadata);

// Records metric for ROLLBACK_POLICY_ACTIVATED event.
void StructuredMetricRollbackPolicyActivated(
    const RollbackMetadata& rollback_metadata);

// TODO(b/261850979): Create methods to report metrics for each Rollback event.
}  // namespace oobe_config

#endif  // OOBE_CONFIG_METRICS_STRUCTURED_METRICS_RECORDER_H_
