// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMINT_CONTEXT_ARC_KEYMINT_METRICS_H_
#define ARC_KEYMINT_CONTEXT_ARC_KEYMINT_METRICS_H_

#include <memory>
#include <string>

class MetricsLibraryInterface;

namespace arc::keymint::context {

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// Any changes must also update the corresponding entries in
// https://crsrc.org/c/tools/metrics/histograms/metadata/arc/enums.xml.
enum class ArcVerifiedBootHashResult {
  kSuccess = 0,
  kInvalidHash = 1,
  kFileError = 2,
  kMaxValue = kFileError
};

enum class ArcVerifiedBootKeyResult {
  kSuccessDevKey = 0,
  kSuccessProdKey = 1,
  kDebugdError = 2,
  kVbLogError = 3,
  kMaxValue = kVbLogError
};

enum class ArcVerifiedBootStateResult {
  kSuccess = 0,
  kNullCrosSystem = 1,
  kInvalidCrosDebug = 2,
  kMaxValue = kInvalidCrosDebug
};

// A class that sends UMA metrics using MetricsLibrary. There is no D-Bus call
// because MetricsLibrary writes the UMA data to /var/lib/metrics/uma-events.
class ArcKeyMintMetrics {
 public:
  ArcKeyMintMetrics();
  ArcKeyMintMetrics(const ArcKeyMintMetrics&) = delete;
  ArcKeyMintMetrics& operator=(const ArcKeyMintMetrics&) = delete;

  ~ArcKeyMintMetrics() = default;

  void SendVerifiedBootHashResult(ArcVerifiedBootHashResult result);
  void SendVerifiedBootKeyResult(ArcVerifiedBootKeyResult result);
  void SendVerifiedBootStateResult(ArcVerifiedBootStateResult result);

  void SetMetricsLibraryForTesting(
      std::unique_ptr<MetricsLibraryInterface> metrics_library);

  MetricsLibraryInterface* metrics_library_for_testing();

 private:
  std::unique_ptr<MetricsLibraryInterface> metrics_library_;
};

}  // namespace arc::keymint::context

#endif  // ARC_KEYMINT_CONTEXT_ARC_KEYMINT_METRICS_H_
