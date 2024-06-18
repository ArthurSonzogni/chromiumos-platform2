// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_METRICS_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_METRICS_UTILS_H_

#include <set>

#include <base/functional/callback_forward.h>

#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom-forward.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom-forward.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom-forward.h"

class MetricsLibraryInterface;

namespace diagnostics {

// Wrap |on_terminal_status_cb| in a repeating callback that invokes
// |on_terminal_status_cb| with the first terminal routine status it receives.
//
// Terminal status mean these enums
// - kPassed
// - kFailed
// - kError
// - kCancelled
// - kFailedToStart
// - kRemoved
// - kUnsupported
// - kNotRun
base::RepeatingCallback<
    void(ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum)>
InvokeOnTerminalStatus(
    base::OnceCallback<
        void(ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum)>
        on_terminal_status_cb);

// Sends the telemetry result (e.g., success or error) to UMA for each category
// in |requested_categories|.
void SendTelemetryResultToUMA(
    MetricsLibraryInterface* metrics,
    const std::set<ash::cros_healthd::mojom::ProbeCategoryEnum>&
        requested_categories,
    const ash::cros_healthd::mojom::TelemetryInfoPtr& info);

// Sends the diagnostic result to UMA. |status| should be a terminal status.
void SendDiagnosticResultToUMA(
    MetricsLibraryInterface* metrics,
    ash::cros_healthd::mojom::DiagnosticRoutineEnum routine,
    ash::cros_healthd::mojom::DiagnosticRoutineStatusEnum status);

// Sends the event subscription usage to UMA.
void SendEventSubscriptionUsageToUMA(
    MetricsLibraryInterface* metrics,
    ash::cros_healthd::mojom::EventCategoryEnum category);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_METRICS_UTILS_H_
