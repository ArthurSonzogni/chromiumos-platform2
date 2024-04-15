// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBPREPROCESSOR_METRICS_H_
#define FBPREPROCESSOR_METRICS_H_

#include <memory>

#include <metrics/metrics_library.h>

#include "fbpreprocessor/firmware_dump.h"

namespace fbpreprocessor {

// The |Metrics| class emits UMA events with libmetrics
// https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/metrics.
// The metrics collected are described in the "Metrics" section of the design
// document go/cros-fbpreprocessord-dd.
class Metrics {
 public:
  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused since they will be used by UMA to
  // interpret the data.
  enum class CollectionAllowedStatus {
    kUnknown = 0,
    kAllowed = 1,
    kDisallowedByPolicy = 2,
    kDisallowedByFinch = 3,
    kDisallowedForMultipleSessions = 4,
    kDisallowedForUserDomain = 5,
    kMaxValue = kDisallowedForUserDomain,
  };

  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused since they will be used by UMA to
  // interpret the data.
  enum class PseudonymizationResult {
    kUnknown = 0,
    kSuccess = 1,
    kFailedToStart = 2,
    kNoOpFailedToMove = 3,
    kMaxValue = kNoOpFailedToMove,
  };

  Metrics();
  ~Metrics();

  // Report whether the collection of firmware dumps is allowed to not and the
  // reason.
  // Emits "Platform.FbPreprocessor.{FirmwareType}.Collection.Allowed".
  bool SendAllowedStatus(FirmwareDump::Type fw_type,
                         CollectionAllowedStatus status);

  // Send the number of firmware dumps of a particular type currently available
  // for collection in the next feedback report. Emitted periodically every 5
  // minutes by |OutputManager|.
  // Emits "Platform.FbPreprocessor.{FirmwareType}.Output.Number".
  bool SendNumberOfAvailableDumps(FirmwareDump::Type fw_type, int num);

  // Send the type of firmware dump that was just pseudonymized.
  // Emits "Platform.FbPreprocessor.Pseudonymization.DumpType".
  bool SendPseudonymizationFirmwareType(FirmwareDump::Type fw_type);

  // Send the status of the pseudonymization operation.
  // Emits "Platform.FbPreprocessor.{FirmwareType}.Pseudonymization.Result".
  bool SendPseudonymizationResult(FirmwareDump::Type fw_type,
                                  PseudonymizationResult result);

  // Instead of using the "real" metrics library that will send events to UMA,
  // unit tests can pass a fake or mock implementation, typically
  // |FakeMetricsLibrary| from metrics/fake_metrics_library.h.
  // The Metrics object will take ownership of the unique_ptr.
  void SetLibraryForTesting(std::unique_ptr<MetricsLibraryInterface> lib);

 private:
  std::unique_ptr<MetricsLibraryInterface> library_;
};

}  // namespace fbpreprocessor

#endif  // FBPREPROCESSOR_METRICS_H_
