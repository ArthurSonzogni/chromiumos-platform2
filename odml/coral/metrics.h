// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_CORAL_METRICS_H_
#define ODML_CORAL_METRICS_H_

#include <string>

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

 private:
  // Helper function for Send*Status methods.
  void SendStatus(const std::string& name, CoralStatus status);

  const raw_ref<MetricsLibraryInterface> metrics_;
};

}  // namespace coral

#endif  // ODML_CORAL_METRICS_H_
