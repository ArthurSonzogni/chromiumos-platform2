// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/metrics.h"

#include <string>

#include <metrics/metrics_library.h>

#include "odml/mojom/coral_service.mojom-shared.h"
#include "odml/mojom/coral_service.mojom.h"

namespace coral {

CoralMetrics::CoralMetrics(raw_ref<MetricsLibraryInterface> metrics)
    : metrics_(metrics) {}

void CoralMetrics::SendStatus(const std::string& name, CoralStatus status) {
  // CoralError starts from 0 and doesn't contain Success. Map "Success" to 0
  // and encode errors to their next integer value.
  int encoded_status =
      status.has_value() ? 0 : (static_cast<int>(status.error()) + 1);
  // exclusive_max is kMaxValue + 2 because kMaxValue is inclusive, and we
  // encode errors to their next value.
  constexpr int exclusive_max =
      static_cast<int>(mojom::CoralError::kMaxValue) + 2;
  metrics_->SendEnumToUMA(name, encoded_status, exclusive_max);
}

void CoralMetrics::SendGroupStatus(CoralStatus status) {
  SendStatus(metrics::kGroupStatus, status);
}

void CoralMetrics::SendCacheEmbeddingsStatus(CoralStatus status) {
  SendStatus(metrics::kCacheEmbeddingsStatus, status);
}

void CoralMetrics::SendEmbeddingEngineStatus(CoralStatus status) {
  SendStatus(metrics::kEmbeddingEngineStatus, status);
}

void CoralMetrics::SendClusteringEngineStatus(CoralStatus status) {
  SendStatus(metrics::kClusteringEngineStatus, status);
}

void CoralMetrics::SendTitleGenerationEngineStatus(CoralStatus status) {
  SendStatus(metrics::kTitleGenerationEngineStatus, status);
}

}  // namespace coral
