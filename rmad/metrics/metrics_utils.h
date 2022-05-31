// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RMAD_METRICS_METRICS_UTILS_H_
#define RMAD_METRICS_METRICS_UTILS_H_

#include <string>
#include <utility>

#include <base/memory/scoped_refptr.h>

#include "rmad/metrics/metrics_constants.h"
#include "rmad/utils/json_store.h"

namespace rmad {

class MetricsUtils {
 public:
  MetricsUtils() = default;
  virtual ~MetricsUtils() = default;

  // Record the metrics to the event-based metrics file, and wait for upload.
  virtual bool Record(scoped_refptr<JsonStore> json_store,
                      bool is_complete) = 0;

  template <typename T>
  static bool GetMetricsValue(scoped_refptr<JsonStore> json_store,
                              const std::string& key,
                              T* result) {
    base::Value metrics = base::Value(base::Value::Type::DICTIONARY);
    if (json_store->GetValue(kMetrics, &metrics)) {
      CHECK(metrics.is_dict());
    }
    return ConvertFromValue(metrics.FindKey(key), result);
  }

  template <typename T>
  static bool SetMetricsValue(scoped_refptr<JsonStore> json_store,
                              const std::string& key,
                              const T& v) {
    base::Value metrics = base::Value(base::Value::Type::DICTIONARY);
    if (json_store->GetValue(kMetrics, &metrics)) {
      CHECK(metrics.is_dict());
    }
    base::Value&& value = ConvertToValue(v);

    const base::Value* result = metrics.FindKey(key);
    if (!result || *result != value) {
      std::optional<base::Value> result_backup =
          result ? std::make_optional(result->Clone()) : std::nullopt;
      metrics.SetKey(key, std::move(value));

      return json_store->SetValue(kMetrics, std::move(metrics));
    }
    return true;
  }
};

}  // namespace rmad

#endif  // RMAD_METRICS_METRICS_UTILS_H_
