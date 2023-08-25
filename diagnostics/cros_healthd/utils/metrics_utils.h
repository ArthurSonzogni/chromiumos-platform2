// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_METRICS_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_METRICS_UTILS_H_

#include <set>

#include <base/functional/callback_forward.h>
#include <metrics/metrics_library.h>

#include "diagnostics/mojom/public/cros_healthd_diagnostics.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"
#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

// These values are logged to UMA. Entries should not be renumbered and
// numeric values should never be reused. Please keep in sync with
// "CrosHealthdTelemetryResult" in tools/metrics/histograms/enums.xml in the
// Chromium repo.
enum class CrosHealthdTelemetryResult {
  kSuccess = 0,
  kError = 1,
  // A special enumerator that must share the highest enumerator value. This
  // value is required when calling |SendEnumToUMA|.
  kMaxValue = kError,
};

// These values are logged to UMA. Entries should not be renumbered and
// numeric values should never be reused. Please keep in sync with
// "CrosHealthdDiagnosticResult" in tools/metrics/histograms/enums.xml in the
// Chromium repo.
enum class CrosHealthdDiagnosticResult {
  kPassed = 0,
  kFailed = 1,
  kError = 2,
  kCancelled = 3,
  kFailedToStart = 4,
  kRemoved = 5,
  kUnsupported = 6,
  kNotRun = 7,
  // A special enumerator that must share the highest enumerator value. This
  // value is required when calling |SendEnumToUMA|.
  kMaxValue = kNotRun,
};

// These values are logged to UMA. Entries should not be renumbered and
// numeric values should never be reused. Please keep in sync with
// "CrosHealthdEventCategory" in tools/metrics/histograms/enums.xml in the
// Chromium repo.
enum class CrosHealthdEventCategory {
  kUnknown = 0,
  kUsb = 1,
  kThunderbolt = 2,
  kLid = 3,
  kBluetooth = 4,
  kPower = 5,
  kAudio = 6,
  kAudioJack = 7,
  kSdCard = 8,
  kNetwork = 9,
  kKeyboardDiagnostic = 10,
  kTouchpad = 11,
  kExternalDisplay = 12,
  kTouchscreen = 13,
  kStylusGarage = 14,
  kStylus = 15,
  kCrash = 16,
  // A special enumerator that must share the highest enumerator value. This
  // value is required when calling |SendEnumToUMA|.
  kMaxValue = kCrash,
};

// Wrap |on_terminal_status_cb| in a repeating callback that invokes
// |on_terminal_status_cb| with the first terminal routine status it receives.
//
// An exception is the kCancelling status. We'll treat it as kCancelled since
// |SubprocRoutine| might never turning into kCancelled status.
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
