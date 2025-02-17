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
inline constexpr char kGroupInputCount[] =
    "Platform.CoralService.Group.InputCount";
inline constexpr char kEmbeddingModelLoaded[] =
    "Platform.CoralService.EmbeddingEngine.ModelLoaded";
inline constexpr char kEmbeddingCacheHit[] =
    "Platform.CoralService.EmbeddingEngine.CacheHit";
inline constexpr char kSafetyVerdictCacheHit[] =
    "Platform.CoralService.EmbeddingEngine.SafetyVerdictCacheHit";
inline constexpr char kSafetyVerdict[] =
    "Platform.CoralService.EmbeddingEngine.SafetyVerdict";
inline constexpr char kLanguageDetectionCacheHit[] =
    "Platform.CoralService.EmbeddingEngine.LanguageDetectionCacheHit";
inline constexpr char kLanguageIsSupported[] =
    "Platform.CoralService.EmbeddingEngine.LanguageSupported";
inline constexpr char kEmbeddingDatabaseEntriesCount[] =
    "Platform.CoralService.EmbeddingEngine.DatabaseEntriesCount";
inline constexpr char kEmbeddingDatabaseDailyWrittenSize[] =
    "Platform.CoralService.EmbeddingEngine.DatabaseDailyWrittenSize";
inline constexpr char kEmbeddingFilteredCount[] =
    "Platform.CoralService.EmbeddingEngine.FilteredCount";
inline constexpr char kClusteringInputCount[] =
    "Platform.CoralService.ClusteringEngine.InputCount";
inline constexpr char kClusteringGeneratedGroupCount[] =
    "Platform.CoralService.ClusteringEngine.GeneratedGroupCount";
inline constexpr char kClusteringGroupItemCount[] =
    "Platform.CoralService.ClusteringEngine.GroupItemCount";
inline constexpr char kTitleGenerationResult[] =
    "Platform.CoralService.TitleGenerationEngine.GenerationResult";
inline constexpr char kTitleGenerationModelLoaded[] =
    "Platform.CoralService.TitleGenerationEngine.ModelLoaded";
inline constexpr char kTitleCacheHit[] =
    "Platform.CoralService.TitleGenerationEngine.CacheHit";
inline constexpr char kTitleCacheDifferenceRatio[] =
    "Platform.CoralService.TitleGenerationEngine.CacheDifferenceRatio";
inline constexpr char kTitleLengthInCharacters[] =
    "Platform.CoralService.TitleGenerationEngine.LengthInCharacters";
inline constexpr char kTitleLengthInWords[] =
    "Platform.CoralService.TitleGenerationEngine.LengthInWords";
inline constexpr char kTitleGenerationInputTokenSize[] =
    "Platform.CoralService.TitleGenerationEngine.InputTokenSize";

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class TitleGenerationResult {
  kSuccess = 0,
  kEmptyModelOutput = 1,
  kMaxValue = kEmptyModelOutput,
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class SafetyVerdict {
  kPass = 0,
  kFail = 1,
  kMaxValue = kFail,
};

}  // namespace metrics

class CoralMetrics {
 public:
  explicit CoralMetrics(raw_ref<MetricsLibraryInterface> metrics);

  // ==========================================================================
  // Success / failure metrics.
  // ==========================================================================

  // Success or error result of Group and CacheEmbeddings operations.
  void SendGroupStatus(CoralStatus status);
  void SendCacheEmbeddingsStatus(CoralStatus status);
  // Success or error result of individual engines.
  void SendEmbeddingEngineStatus(CoralStatus status);
  void SendClusteringEngineStatus(CoralStatus status);
  void SendTitleGenerationEngineStatus(CoralStatus status);

  // ==========================================================================
  // Latency-related metrics. These are only reported when the operation
  // completes successfully.
  // ==========================================================================

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

  // ==========================================================================
  // Engine-specific metrics.
  // ==========================================================================

  // Number of input entities in the Group request.
  void SendGroupInputCount(int count);

  // --------------------------------------------------------------------------
  // Embedding engine metrics.
  // --------------------------------------------------------------------------
  // Whether the model is already loaded when receiving a request.
  void SendEmbeddingModelLoaded(bool is_loaded);
  // Whether a request entity already has its embedding cached.
  void SendEmbeddingCacheHit(bool is_cache_hit);
  // Whether a request entity already has its safety verdict cached.
  void SendSafetyVerdictCacheHit(bool is_cache_hit);
  // The request entity's safety verdict.
  void SendSafetyVerdict(metrics::SafetyVerdict verdict);
  // Whether a request entity already has its language detection result cached.
  void SendLanguageDetectionCacheHit(bool is_cache_hit);
  // Whether the request entity's language is supported.
  void SendLanguageIsSupported(bool is_supported);
  // Send the number of entries in the embedding database every time a user
  // session starts.
  void SendEmbeddingDatabaseEntriesCount(int count);
  // Send the total size (in KiB) written to the disk in the past day,
  // recorded daily.
  void SendEmbeddingDatabaseDailyWrittenSize(int size_in_bytes);
  // Number of entries filtered by the embedding engine. This might be because
  // the entity doesn't pass safety check.
  void SendEmbeddingFilteredCount(int count);

  // --------------------------------------------------------------------------
  // Clustering engine metrics.
  // --------------------------------------------------------------------------
  // Number of input entities in the clustering request.
  void SendClusteringInputCount(int count);
  // Number of generated groups that satisfies the output requirement (having at
  // least 4 items). This number isn't affected by the maximum number of groups
  // the clustering engine can eventually return (2).
  void SendGeneratedGroupCount(int count);
  // Send the number of items for each group in the cluster result. This can be
  // anywhere from 1 (ungrouped stuff) to the max input size, unaffected by the
  // output requirement we set on the generated group size (4 to 10).
  void SendGroupItemCount(int count);

  // --------------------------------------------------------------------------
  // TitleGeneration engine metrics.
  // --------------------------------------------------------------------------
  // Whether a non-empty title is successfully generated, or the corresponding
  // error reason if not.
  void SendTitleGenerationResult(metrics::TitleGenerationResult result);
  // Whether the model is already loaded when receiving a request.
  void SendTitleGenerationModelLoaded(bool is_loaded);
  // Whether a request group finds a title to reuse in the cache.
  void SendTitleCacheHit(bool is_cache_hit);
  // The lowest difference ratio found in the title cache. If this is lower than
  // the threshold, `is_cache_hit` above will be true. The ratio could be larger
  // than 1.0, but we cap it to 1.0 when reporting metrics. This is used to
  // determine the best threshold to set, and we won't set the threshold
  // above 1.0 anyway.
  void SendTitleCacheDifferenceRatio(float ratio);
  // Number of chars (i.e., bytes) in the generated title.
  void SendTitleLengthInCharacters(int count);
  // Number of "words" in the generated title, defined by "number of white
  // spaces + 1", to represent the number of English words in a title. This is
  // not useful in languages other than English, and should only be reported for
  // English titles after we support i18n.
  void SendTitleLengthInWords(int count);
  // The token size of the title generation prompt.
  void SendTitleInputTokenSize(int count);

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
