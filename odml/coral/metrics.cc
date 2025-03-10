// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/coral/metrics.h"

#include <algorithm>
#include <string>

#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include "odml/mojom/coral_service.mojom-shared.h"
#include "odml/mojom/coral_service.mojom.h"

namespace coral {

namespace {

constexpr int kKbScale = 1024;

}

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

void CoralMetrics::SendTitleGenerationResult(
    metrics::TitleGenerationResult result) {
  metrics_->SendEnumToUMA(metrics::kTitleGenerationResult, result);
}

void CoralMetrics::SendLatency(const std::string& name,
                               base::TimeDelta sample,
                               base::TimeDelta max) {
  metrics_->SendTimeToUMA(name, sample, base::Milliseconds(1), max, 50);
}

void CoralMetrics::SendMediumLatency(const std::string& name,
                                     base::TimeDelta sample) {
  SendLatency(name, sample, base::Seconds(30));
}

void CoralMetrics::SendGroupLatency(base::TimeDelta duration) {
  SendMediumLatency(metrics::kGroupLatency, duration);
}

void CoralMetrics::SendCacheEmbeddingsLatency(base::TimeDelta duration) {
  SendMediumLatency(metrics::kCacheEmbeddingsLatency, duration);
}

void CoralMetrics::SendEmbeddingEngineLatency(base::TimeDelta duration) {
  SendMediumLatency(metrics::kEmbeddingEngineLatency, duration);
}

void CoralMetrics::SendClusteringEngineLatency(base::TimeDelta duration) {
  SendLatency(metrics::kClusteringEngineLatency, duration, base::Seconds(2));
}

void CoralMetrics::SendTitleGenerationEngineLatency(base::TimeDelta duration) {
  SendMediumLatency(metrics::kTitleGenerationEngineLatency, duration);
}

void CoralMetrics::SendLoadEmbeddingModelLatency(base::TimeDelta duration) {
  SendMediumLatency(metrics::kLoadEmbeddingModelLatency, duration);
}

void CoralMetrics::SendGenerateEmbeddingLatency(base::TimeDelta duration) {
  SendLatency(metrics::kGenerateEmbeddingLatency, duration, base::Seconds(2));
}

void CoralMetrics::SendLoadTitleGenerationModelLatency(
    base::TimeDelta duration) {
  SendMediumLatency(metrics::kLoadTitleGenerationModelLatency, duration);
}

void CoralMetrics::SendGenerateTitleLatency(base::TimeDelta duration) {
  SendLatency(metrics::kGenerateTitleLatency, duration, base::Seconds(10));
}

void CoralMetrics::SendGroupInputCount(int count) {
  metrics_->SendToUMA(metrics::kGroupInputCount, count, 1, 101, 50);
}

void CoralMetrics::SendEmbeddingModelLoaded(bool is_loaded) {
  metrics_->SendBoolToUMA(metrics::kEmbeddingModelLoaded, is_loaded);
}

void CoralMetrics::SendEmbeddingCacheHit(bool is_cache_hit) {
  metrics_->SendBoolToUMA(metrics::kEmbeddingCacheHit, is_cache_hit);
}

void CoralMetrics::SendSafetyVerdictCacheHit(bool is_cache_hit) {
  metrics_->SendBoolToUMA(metrics::kSafetyVerdictCacheHit, is_cache_hit);
}

void CoralMetrics::SendSafetyVerdict(metrics::SafetyVerdict verdict) {
  metrics_->SendEnumToUMA(metrics::kSafetyVerdict, verdict);
}

void CoralMetrics::SendLanguageDetectionCacheHit(bool is_cache_hit) {
  metrics_->SendBoolToUMA(metrics::kLanguageDetectionCacheHit, is_cache_hit);
}

void CoralMetrics::SendLanguageIsSupported(bool is_supported) {
  metrics_->SendBoolToUMA(metrics::kLanguageIsSupported, is_supported);
}

void CoralMetrics::SendEmbeddingDatabaseEntriesCount(int count) {
  metrics_->SendToUMA(metrics::kEmbeddingDatabaseEntriesCount, count, 1, 1001,
                      50);
}

void CoralMetrics::SendEmbeddingDatabaseDailyWrittenSize(int size_in_bytes) {
  metrics_->SendToUMA(metrics::kEmbeddingDatabaseDailyWrittenSize,
                      size_in_bytes / kKbScale, 10 /*10KB*/, 1000000 /*1GB*/,
                      100);
}

void CoralMetrics::SendEmbeddingFilteredCount(int count) {
  metrics_->SendToUMA(metrics::kEmbeddingFilteredCount, count, 1, 101, 50);
}

void CoralMetrics::SendClusteringInputCount(int count) {
  metrics_->SendToUMA(metrics::kClusteringInputCount, count, 1, 101, 50);
}

void CoralMetrics::SendGeneratedGroupCount(int count) {
  metrics_->SendLinearToUMA(metrics::kClusteringGeneratedGroupCount, count,
                            101);
}
void CoralMetrics::SendGroupItemCount(int count) {
  metrics_->SendLinearToUMA(metrics::kClusteringGroupItemCount, count, 101);
}

void CoralMetrics::SendTitleGenerationEngineStatus(CoralStatus status) {
  SendStatus(metrics::kTitleGenerationEngineStatus, status);
}

void CoralMetrics::SendTitleGenerationModelLoaded(bool is_loaded) {
  metrics_->SendBoolToUMA(metrics::kTitleGenerationModelLoaded, is_loaded);
}

void CoralMetrics::SendTitleCacheHit(bool is_cache_hit) {
  metrics_->SendBoolToUMA(metrics::kTitleCacheHit, is_cache_hit);
}

void CoralMetrics::SendTitleCacheDifferenceRatio(float ratio) {
  int percentage = std::min(100, static_cast<int>(ratio * 100));
  metrics_->SendPercentageToUMA(metrics::kTitleCacheDifferenceRatio,
                                percentage);
}

void CoralMetrics::SendTitleLengthInCharacters(int count) {
  metrics_->SendLinearToUMA(metrics::kTitleLengthInCharacters, count, 101);
}

void CoralMetrics::SendTitleLengthInWords(int count) {
  metrics_->SendLinearToUMA(metrics::kTitleLengthInWords, count, 101);
}

void CoralMetrics::SendTitleInputTokenSize(int count) {
  metrics_->SendToUMA(metrics::kTitleGenerationInputTokenSize, count, 1, 1025,
                      50);
}

void CoralMetrics::SendTitleDatabaseDailyWrittenSize(int size_in_bytes) {
  metrics_->SendToUMA(metrics::kTitleDatabaseDailyWrittenSize, size_in_bytes,
                      1 /*1 byte*/, 20480 /*10KiB*/, 100);
}

}  // namespace coral
