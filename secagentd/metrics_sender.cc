// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "secagentd/metrics_sender.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/time/time.h"
#include "metrics/metrics_library.h"

namespace secagentd {
namespace metrics {
bool operator==(const CountMetric& m1, const CountMetric& m2) {
  if (std::string_view(m1.name) == std::string_view(m2.name) &&
      m1.min == m2.min && m1.max == m2.max && m1.nbuckets == m2.nbuckets) {
    return true;
  }
  return false;
}
}  // namespace metrics

MetricsSender& MetricsSender::GetInstance() {
  static base::NoDestructor<MetricsSender> instance;
  return *instance;
}

void MetricsSender::InitBatchedMetrics() {
  flush_batched_metrics_timer_.Start(
      FROM_HERE, base::Seconds(metrics::kBatchTimer),
      base::BindRepeating(&MetricsSender::Flush, base::Unretained(this)));
}

void MetricsSender::IncrementCountMetric(metrics::CountMetric m, int value) {
  unsigned int scaled_value = value / m.nbuckets;

  // properly round down if negative.
  if (value < 0 && abs(value) % m.nbuckets > m.nbuckets / 2) {
    // round down.
    scaled_value -= 1;
  }
  batch_count_map_[m][scaled_value] += 1;
  if (batch_count_map_[m][scaled_value] >= metrics::kMaxMapValue) {
    Flush();
  }
}

void MetricsSender::Flush() {
  task_runner_->PostTask(
      FROM_HERE, base::BindOnce(&MetricsSender::SendBatchedMetricsToUMA,
                                weak_ptr_factory_.GetWeakPtr(), batch_enum_map_,
                                batch_count_map_));
  batch_enum_map_.clear();
  batch_count_map_.clear();

  // Run registered callbacks.
  for (auto cb : metric_callbacks_) {
    cb.Run();
  }
}

void MetricsSender::SendBatchedMetricsToUMA(
    metrics::MetricsMap enum_map_copy,
    metrics::MetricsCountMap count_map_copy) {
  // Commit enum histogram metrics.
  for (auto const& [key, val] : enum_map_copy) {
    int pos = key.find_last_of(":");
    auto metric_name = key.substr(0, pos);
    auto sample = stoi(key.substr(pos + 1));
    auto it = exclusive_max_map_.find(metric_name.c_str());

    // If sample is success value divide by 100.
    int count = val;
    if (sample == success_value_map_.find(metric_name)->second) {
      count = (count + 100 - 1) / 100;
    }

    if (!metrics_library_->SendRepeatedEnumToUMA(
            base::StrCat({metrics::kMetricNamePrefix, metric_name}), sample,
            it->second, count)) {
      LOG(ERROR) << "Failed to send batched metrics for " << metric_name;
    }
  }
  // Commit count histogram metrics.
  for (auto const& [metric, submap] : count_map_copy) {
    std::string metric_name =
        base::StrCat({metrics::kMetricNamePrefix, metric.name});
    for (auto const& [sample, nsample] : submap) {
      // sample was scaled down on storage to conserve memory, scale it back up.
      metrics_library_->SendRepeatedToUMA(metric_name, sample * metric.nbuckets,
                                          metric.min, metric.max,
                                          metric.nbuckets, nsample);
    }
  }
}

void MetricsSender::RegisterMetricOnFlushCallback(
    base::RepeatingCallback<void()> cb) {
  metric_callbacks_.push_back(std::move(cb));
}

MetricsSender::MetricsSender()
    : MetricsSender(std::make_unique<MetricsLibrary>()) {}

MetricsSender::MetricsSender(
    std::unique_ptr<MetricsLibraryInterface> metrics_library)
    : weak_ptr_factory_(this), metrics_library_(std::move(metrics_library)) {}

}  // namespace secagentd
