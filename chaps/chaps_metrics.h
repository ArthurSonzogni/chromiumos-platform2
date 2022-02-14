// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_CHAPS_METRICS_H_
#define CHAPS_CHAPS_METRICS_H_

#include <string>

#include <base/time/time.h>
#include <metrics/metrics_library.h>

namespace chaps {

constexpr char kReinitializingToken[] = "Platform.Chaps.ReinitializingToken";

constexpr char kTPMAvailability[] = "Platform.Chaps.TPMAvailability";

constexpr char kDatabaseCorrupted[] = "Chaps.DatabaseCorrupted";

constexpr char kDatabaseRepairFailure[] = "Chaps.DatabaseRepairFailure";

constexpr char kDatabaseCreateFailure[] = "Chaps.DatabaseCreateFailure";

constexpr char kDatabaseOpenedSuccessfully[] =
    "Chaps.DatabaseOpenedSuccessfully";

constexpr char kDatabaseOpenAttempt[] = "Chaps.DatabaseOpenAttempt";

// List of reasons to initializing token. These entries
// should not be renumbered and numeric values should never be reused.
// These values are persisted to logs.
enum class ReinitializingTokenStatus {
  kFailedToUnseal = 0,
  kBadAuthorizationData = 1,
  kFailedToDecryptRootKey = 2,
  kFailedToValidate = 3,
  kMaxValue
};

// The TPM availability status. These entries
// should not be renumbered and numeric values should never be reused.
// These values are persisted to logs.
enum class TPMAvailabilityStatus {
  kTPMAvailable = 0,
  kTPMUnavailable = 1,
  kMaxValue
};

// This class provides wrapping functions for callers to report Chaps related
// metrics without bothering to know all the constant declarations.
class ChapsMetrics : private MetricsLibrary {
 public:
  ChapsMetrics() = default;
  ChapsMetrics(const ChapsMetrics&) = delete;
  ChapsMetrics& operator=(const ChapsMetrics&) = delete;

  virtual ~ChapsMetrics() = default;

  // The |status| value is reported to the
  // "Platform.Chaps.ReinitializingToken" enum histogram.
  virtual void ReportReinitializingTokenStatus(
      ReinitializingTokenStatus status);

  // The |status| value is reported to the "Platform.Chaps.TPMAvailability" enum
  // histogram.
  virtual void ReportTPMAvailabilityStatus(TPMAvailabilityStatus status);

  // Cros events are translated to an enum and reported to the generic
  // "Platform.CrOSEvent" enum histogram. The |event| string must be registered
  // in metrics/metrics_library.cc:kCrosEventNames.
  virtual void ReportCrosEvent(const std::string& event);

  void set_metrics_library_for_testing(
      MetricsLibraryInterface* metrics_library) {
    metrics_library_ = metrics_library;
  }

 private:
  MetricsLibraryInterface* metrics_library_{this};
};

}  // namespace chaps

#endif  // CHAPS_CHAPS_METRICS_H_
