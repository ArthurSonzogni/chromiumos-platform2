// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_METRICS_H_
#define ODML_CORAL_METRICS_H_

#include <string>

#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include "odml/coral/common.h"

namespace coral {

namespace metrics {

inline constexpr char kGroupStatus[] = "Platform.CoralService.Error.Group";
inline constexpr char kCacheEmbeddingsStatus[] =
    "Platform.CoralService.Error.CacheEmbeddings";
inline constexpr char kEmbeddingEngineStatus[] =
    "Platform.CoralService.Error.EmbeddingEngine";
inline constexpr char kClusteringEngineStatus[] =
    "Platform.CoralService.Error.ClusteringEngine";
inline constexpr char kTitleGenerationEngineStatus[] =
    "Platform.CoralService.Error.TitleGenerationEngine";
inline constexpr char kTitleGenerationResult[] =
    "Platform.CoralService.TitleGenerationEngine.GenerationResult";
inline constexpr char kGroupLatency[] = "Platform.CoralService.Latency.Group";
inline constexpr char kCacheEmbeddingsLatency[] =
    "Platform.CoralService.Latency.CacheEmbeddings";
inline constexpr char kEmbeddingEngineLatency[] =
    "Platform.CoralService.Latency.EmbeddingEngine";
inline constexpr char kClusteringEngineLatency[] =
    "Platform.CoralService.Latency.ClusteringEngine";
inline constexpr char kTitleGenerationEngineLatency[] =
    "Platform.CoralService.Latency.TitleGenerationEngine";
inline constexpr char kLoadEmbeddingModelLatency[] =
    "Platform.CoralService.Latency.EmbeddingEngine.LoadModel";
inline constexpr char kGenerateEmbeddingLatency[] =
    "Platform.CoralService.Latency.EmbeddingEngine.GenerateEmbedding";
inline constexpr char kLoadTitleGenerationModelLatency[] =
    "Platform.CoralService.Latency.TitleGenerationEngine.LoadModel";
inline constexpr char kGenerateTitleLatency[] =
    "Platform.CoralService.Latency.TitleGenerationEngine.GenerateTitle";

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class TitleGenerationResult {
  kSuccess = 0,
  kEmptyModelOutput = 1,
  kMaxValue = kEmptyModelOutput,
};

}  // namespace metrics

class CoralMetrics {
 public:
  explicit CoralMetrics(raw_ref<MetricsLibraryInterface> metrics);

  // Success or error result of Group and CacheEmbeddings operations.
  void SendGroupStatus(CoralStatus status);
  void SendCacheEmbeddingsStatus(CoralStatus status);
  // Success or error result of individual engines.
  void SendEmbeddingEngineStatus(CoralStatus status);
  void SendClusteringEngineStatus(CoralStatus status);
  void SendTitleGenerationEngineStatus(CoralStatus status);

  // Whether a non-empty title is successfully generated, or the corresponding
  // error reason if not.
  void SendTitleGenerationResult(metrics::TitleGenerationResult result);

  // Latency-related metrics. These are only reported when the operation
  // completes successfully.

  // Latency of Group and CacheEmbeddings operations.
  void SendGroupLatency(base::TimeDelta duration);
  void SendCacheEmbeddingsLatency(base::TimeDelta duration);

  // Latency of individual engines.
  void SendEmbeddingEngineLatency(base::TimeDelta duration);
  void SendClusteringEngineLatency(base::TimeDelta duration);
  // This counts towards all titles are generated, even in async title
  // generation mode.
  void SendTitleGenerationEngineLatency(base::TimeDelta duration);

  // Breakdown of some operations in individual engines.
  void SendLoadEmbeddingModelLatency(base::TimeDelta duration);
  void SendGenerateEmbeddingLatency(base::TimeDelta duration);
  void SendLoadTitleGenerationModelLatency(base::TimeDelta duration);
  void SendGenerateTitleLatency(base::TimeDelta duration);

 private:
  // Helper function for Send*Status methods.
  void SendStatus(const std::string& name, CoralStatus status);
  // Helper function for Send*Latency methods.
  void SendLatency(const std::string& name,
                   base::TimeDelta sample,
                   base::TimeDelta max);
  void SendMediumLatency(const std::string& name, base::TimeDelta sample);

  const raw_ref<MetricsLibraryInterface> metrics_;
};

}  // namespace coral

#endif  // ODML_CORAL_METRICS_H_
