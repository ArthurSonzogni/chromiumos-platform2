// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CROS_SAFETY_METRICS_H_
#define ODML_CROS_SAFETY_METRICS_H_

#include <string>

#include <base/containers/fixed_flat_map.h>
#include <metrics/metrics_library.h>

#include "odml/mojom/cros_safety.mojom-shared.h"
#include "odml/mojom/cros_safety_service.mojom-shared.h"

namespace cros_safety {

namespace metrics {

inline constexpr char kGetOnDeviceSafetySession[] =
    "Platform.SafetyService.Error.GetOnDeviceSafetySession";
inline constexpr char kGetCloudSafetySession[] =
    "Platform.SafetyService.Error.GetOnDeviceSafetySession";
inline constexpr char kClassifySafetyResultPrefix[] =
    "Platform.SafetyService.ClassifySafetyResult.";
inline constexpr char kClassifySafetyLatencyPrefix[] =
    "Platform.SafetyService.ClassifySafetyLatency.";

constexpr auto kMapRulesetToString =
    base::MakeFixedFlatMap<mojom::SafetyRuleset, std::string>({
        {mojom::SafetyRuleset::kGeneric, "Generic"},
        {mojom::SafetyRuleset::kMantis, "Mantis"},
        {mojom::SafetyRuleset::kMantisInputImage, "MantisInputImage"},
        {mojom::SafetyRuleset::kMantisOutputImage, "MantisOutputImage"},
        {mojom::SafetyRuleset::kMantisGeneratedRegion, "MantisGeneratedRegion"},
        {mojom::SafetyRuleset::kCoral, "Coral"},
    });

}  // namespace metrics

class SafetyMetrics {
 public:
  explicit SafetyMetrics(raw_ref<MetricsLibraryInterface> metrics);

  // Success or error result of getting safety sessions.
  void SendGetOnDeviceSafetySessionResult(
      mojom::GetOnDeviceSafetySessionResult result);
  void SendGetCloudSafetySessionResult(
      mojom::GetCloudSafetySessionResult result);

  // Success or error result of safety filtering.
  void SendClassifySafetyResult(
      cros_safety::mojom::SafetyRuleset ruleset,
      cros_safety::mojom::SafetyClassifierVerdict verdict);

  // Round trip time for safety filtering.
  void SendClassifySafetyLatency(cros_safety::mojom::SafetyRuleset ruleset,
                                 base::TimeDelta duration);

 private:
  const raw_ref<MetricsLibraryInterface> metrics_;
};

}  // namespace cros_safety

#endif  // ODML_CROS_SAFETY_METRICS_H_
