// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_METRICS_ENTERPRISE_ROLLBACK_METRICS_RECORDER_H_
#define OOBE_CONFIG_METRICS_ENTERPRISE_ROLLBACK_METRICS_RECORDER_H_

#include "oobe_config/metrics/enterprise_rollback_metrics_data.pb.h"

namespace oobe_config {

void RecordEnterpriseRollbackMetric(const EventData& event_data,
                                    const RollbackMetadata& rollback_metadata);

}  // namespace oobe_config

#endif  // OOBE_CONFIG_METRICS_ENTERPRISE_ROLLBACK_METRICS_RECORDER_H_
