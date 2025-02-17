// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/cros_safety/metrics.h"

#include <string>

#include "odml/mojom/cros_safety.mojom-shared.h"
#include "odml/mojom/cros_safety_service.mojom-shared.h"

namespace cros_safety {

namespace {

using mojom::GetCloudSafetySessionResult;
using mojom::GetOnDeviceSafetySessionResult;
using mojom::SafetyClassifierVerdict;
using mojom::SafetyRuleset;

}  // namespace

SafetyMetrics::SafetyMetrics(raw_ref<MetricsLibraryInterface> metrics)
    : metrics_(metrics) {}

void SafetyMetrics::SendGetOnDeviceSafetySessionResult(
    GetOnDeviceSafetySessionResult result) {
  metrics_->SendEnumToUMA(metrics::kGetOnDeviceSafetySession, result);
}

void SafetyMetrics::SendGetCloudSafetySessionResult(
    GetCloudSafetySessionResult result) {
  metrics_->SendEnumToUMA(metrics::kGetCloudSafetySession, result);
}

void SafetyMetrics::SendClassifySafetyResult(SafetyRuleset ruleset,
                                             SafetyClassifierVerdict verdict) {
  if (!metrics::kMapRulesetToString.contains(ruleset)) {
    LOG(WARNING) << "Cannot report histogram for unknown ruleset.";
    return;
  }
  metrics_->SendEnumToUMA(metrics::kClassifySafetyResultPrefix +
                              metrics::kMapRulesetToString.at(ruleset),
                          verdict);
}

void SafetyMetrics::SendClassifySafetyLatency(SafetyRuleset ruleset,
                                              base::TimeDelta duration) {
  if (!metrics::kMapRulesetToString.contains(ruleset)) {
    LOG(WARNING) << "Cannot report histogram for unknown ruleset.";
    return;
  }
  metrics_->SendTimeToUMA(metrics::kClassifySafetyLatencyPrefix +
                              metrics::kMapRulesetToString.at(ruleset),
                          duration, base::Milliseconds(1), base::Seconds(30),
                          50);
}

}  // namespace cros_safety
