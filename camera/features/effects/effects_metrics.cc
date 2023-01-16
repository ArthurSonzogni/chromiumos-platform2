/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/effects/effects_metrics.h"

#include <numeric>
#include <utility>

namespace {
cros::CameraEffect CameraEffectFromConfig(cros::EffectsConfig config) {
  if (config.blur_enabled && config.relight_enabled) {
    return cros::CameraEffect::kBlurAndRelight;
  } else if (config.blur_enabled) {
    return cros::CameraEffect::kBlur;
  } else if (config.relight_enabled) {
    return cros::CameraEffect::kRelight;
  }
  return cros::CameraEffect::kNone;
}

}  // namespace

namespace cros {

void EffectsMetricsData::RecordSelectedEffect(const EffectsConfig& config) {
  if (config.HasEnabledEffects()) {
    selected_effects_.insert(CameraEffectFromConfig(config));
  }
}

// TODO(b/265602808): record blob stream processing latency
void EffectsMetricsData::RecordFrameProcessingLatency(
    const EffectsConfig& config, const base::TimeDelta& latency) {
  size_t idx = static_cast<size_t>(CameraEffectFromConfig(config));
  processing_times_[idx].push_back(latency);
}

// TODO(b/265602808): record blob stream frame interval
void EffectsMetricsData::RecordFrameProcessingInterval(
    const EffectsConfig& config, const base::TimeDelta& interval) {
  size_t idx = static_cast<size_t>(CameraEffectFromConfig(config));
  frame_intervals_[idx].push_back(interval);
}

bool EffectsMetricsData::EffectSelected(CameraEffect effect) const {
  return selected_effects_.contains(effect);
}

base::TimeDelta EffectsMetricsData::AverageFrameProcessingLatency(
    CameraEffect effect) const {
  auto latencies = processing_times_[static_cast<size_t>(effect)];
  if (latencies.size() > 0) {
    return std::reduce(latencies.begin(), latencies.end()) / latencies.size();
  }
  return base::TimeDelta();
}

base::TimeDelta EffectsMetricsData::AverageFrameProcessingInterval(
    CameraEffect effect) const {
  auto intervals = frame_intervals_[static_cast<size_t>(effect)];
  if (intervals.size() > 0) {
    return std::reduce(intervals.begin(), intervals.end()) / intervals.size();
  }
  return base::TimeDelta();
}

EffectsMetricsUploader::EffectsMetricsUploader(
    scoped_refptr<base::SequencedTaskRunner> task_runner)
    : metrics_helper_(CameraMetrics::New()),
      last_upload_time_(base::TimeTicks::Now()),
      task_runner_(task_runner) {}

base::TimeDelta EffectsMetricsUploader::TimeSinceLastUpload() {
  base::AutoLock lock(lock_);
  return base::TimeTicks::Now() - last_upload_time_;
}

void EffectsMetricsUploader::UploadMetricsData(EffectsMetricsData metrics) {
  base::AutoLock lock(lock_);
  last_upload_time_ = base::TimeTicks::Now();

  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&EffectsMetricsUploader::UploadMetricsDataOnThread,
                     base::Unretained(this), std::move(metrics)));
}

void EffectsMetricsUploader::UploadMetricsDataOnThread(
    EffectsMetricsData metrics) {
  // TODO(b/265602808): upload blob stream metrics
  // Post per-effect metrics
  for (int i = 0; i <= static_cast<int>(CameraEffect::kMaxValue); i++) {
    CameraEffect effect = static_cast<CameraEffect>(i);

    if (metrics.EffectSelected(effect)) {
      metrics_helper_->SendEffectsSelectedEffect(effect);
    }

    auto avg_latency = metrics.AverageFrameProcessingLatency(effect);
    if (avg_latency != base::TimeDelta()) {
      metrics_helper_->SendEffectsAvgProcessingLatency(
          effect, CameraEffectStreamType::kYuv, avg_latency);
    }

    auto avg_interval = metrics.AverageFrameProcessingInterval(effect);
    if (avg_interval != base::TimeDelta()) {
      metrics_helper_->SendEffectsAvgProcessedFrameInterval(
          effect, CameraEffectStreamType::kYuv, avg_interval);
    }
  }
}

}  // namespace cros
