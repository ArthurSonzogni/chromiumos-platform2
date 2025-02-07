// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_keymint_metrics.h"

#include <utility>

#include <base/logging.h>
#include <metrics/metrics_library.h>

namespace arc::keymint::context {

namespace {

// The string values need to be the same as in
// https://crsrc.org/c/tools/metrics/histograms/metadata/arc/histograms.xml.
constexpr char kVerifiedBootKeyStatusHistogram[] =
    "Arc.KeyMint.VerifiedBootKey.Result";
constexpr char kVerifiedBootHashStatusHistogram[] =
    "Arc.KeyMint.VerifiedBootHash.Result";
constexpr char kVerifiedBootStateStatusHistogram[] =
    "Arc.KeyMint.VerifiedBootState.Result";

}  // namespace

ArcKeyMintMetrics::ArcKeyMintMetrics()
    : metrics_library_(std::make_unique<MetricsLibrary>()) {}

MetricsLibraryInterface* ArcKeyMintMetrics::metrics_library_for_testing() {
  return metrics_library_.get();
}

void ArcKeyMintMetrics::SendVerifiedBootHashResult(
    ArcVerifiedBootHashResult result) {
  if (metrics_library_ == nullptr) {
    LOG(ERROR) << "Not recording verified boot hash result because "
                  "metrics_library_ is null";
  }
  metrics_library_->SendEnumToUMA(
      kVerifiedBootHashStatusHistogram, static_cast<uint64_t>(result),
      static_cast<uint64_t>(ArcVerifiedBootHashResult::kMaxValue) + 1);
}

void ArcKeyMintMetrics::SendVerifiedBootKeyResult(
    ArcVerifiedBootKeyResult result) {
  if (metrics_library_ == nullptr) {
    LOG(ERROR) << "Not recording verified boot key result because "
                  "metrics_library_ is null";
  }
  metrics_library_->SendEnumToUMA(
      kVerifiedBootKeyStatusHistogram, static_cast<uint64_t>(result),
      static_cast<uint64_t>(ArcVerifiedBootKeyResult::kMaxValue) + 1);
}

void ArcKeyMintMetrics::SendVerifiedBootStateResult(
    ArcVerifiedBootStateResult result) {
  if (metrics_library_ == nullptr) {
    LOG(ERROR) << "Not recording verified boot state result because "
                  "metrics_library_ is null";
  }
  metrics_library_->SendEnumToUMA(
      kVerifiedBootStateStatusHistogram, static_cast<uint64_t>(result),
      static_cast<uint64_t>(ArcVerifiedBootStateResult::kMaxValue) + 1);
}

void ArcKeyMintMetrics::SetMetricsLibraryForTesting(
    std::unique_ptr<MetricsLibraryInterface> metrics_library) {
  metrics_library_ = std::move(metrics_library);
}

}  // namespace arc::keymint::context
