// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "features/frame_drop_monitor/frame_drop_monitor_stream_manipulator.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

#include <base/containers/contains.h>

namespace cros {

namespace {

constexpr int32_t kMinExpectedFps = 15;
constexpr int64_t kNanoSecondsPerSecond = 1000000000;

std::optional<int64_t> TryGetSensorTimestamp(Camera3CaptureDescriptor* desc) {
  base::span<const int64_t> timestamp =
      desc->GetMetadata<int64_t>(ANDROID_SENSOR_TIMESTAMP);
  return timestamp.size() == 1 ? std::make_optional(timestamp[0])
                               : std::nullopt;
}

bool HasEnabledEffects(cros::mojom::EffectsConfigPtr effects_config) {
  return effects_config->blur_enabled || effects_config->relight_enabled ||
         effects_config->replace_enabled || effects_config->retouch_enabled ||
         effects_config->studio_look_enabled;
}

}  // namespace

//
// FrameDropMonitorStreamManipulator implementations.
//

FrameDropMonitorStreamManipulator::FrameDropMonitorStreamManipulator(
    RuntimeOptions* runtime_options,
    bool auto_framing_supported,
    bool effects_supported,
    bool hdrnet_supported)
    : runtime_options_(runtime_options),
      auto_framing_supported_(auto_framing_supported),
      effects_supported_(effects_supported),
      hdrnet_supported_(hdrnet_supported),
      camera_metrics_(CameraMetrics::New()),
      thread_("FrameDropMonitorThread") {
  CHECK(thread_.Start());
}

FrameDropMonitorStreamManipulator::~FrameDropMonitorStreamManipulator() {
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&FrameDropMonitorStreamManipulator::UploadMetricsOnThread,
                     base::Unretained(this)));
  thread_.Stop();
}

bool FrameDropMonitorStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  bool ret = false;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&FrameDropMonitorStreamManipulator::InitializeOnThread,
                     base::Unretained(this), static_info, callbacks),
      &ret);
  return ret;
}

bool FrameDropMonitorStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  bool ret = false;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(
          &FrameDropMonitorStreamManipulator::ConfigureStreamsOnThread,
          base::Unretained(this), stream_config),
      &ret);
  return ret;
}

bool FrameDropMonitorStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  bool ret = false;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(
          &FrameDropMonitorStreamManipulator::OnConfiguredStreamsOnThread,
          base::Unretained(this), stream_config),
      &ret);
  return ret;
}

bool FrameDropMonitorStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  return true;
}

bool FrameDropMonitorStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  bool ret = false;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(
          &FrameDropMonitorStreamManipulator::ProcessCaptureRequestOnThread,
          base::Unretained(this), request),
      &ret);
  return ret;
}

bool FrameDropMonitorStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor result) {
  bool ret = false;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(
          &FrameDropMonitorStreamManipulator::ProcessCaptureResultOnThread,
          base::Unretained(this), &result),
      &ret);
  callbacks_.result_callback.Run(std::move(result));
  return ret;
}

void FrameDropMonitorStreamManipulator::Notify(camera3_notify_msg_t msg) {
  callbacks_.notify_callback.Run(std::move(msg));
}

bool FrameDropMonitorStreamManipulator::Flush() {
  return true;
}

bool FrameDropMonitorStreamManipulator::InitializeOnThread(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  CHECK(thread_.IsCurrentThread());

  callbacks_ = std::move(callbacks);
  partial_result_count_ = GetPartialResultCount(static_info);

  return true;
}

bool FrameDropMonitorStreamManipulator::ConfigureStreamsOnThread(
    Camera3StreamConfiguration* stream_config) {
  CHECK(thread_.IsCurrentThread());

  ResetOnThread();

  for (auto* s : stream_config->GetStreams()) {
    if (s->format == HAL_PIXEL_FORMAT_BLOB) {
      blob_stream_ = s;
    }
  }

  return true;
}

bool FrameDropMonitorStreamManipulator::OnConfiguredStreamsOnThread(
    Camera3StreamConfiguration* stream_config) {
  CHECK(thread_.IsCurrentThread());

  return true;
}

bool FrameDropMonitorStreamManipulator::ProcessCaptureRequestOnThread(
    Camera3CaptureDescriptor* request) {
  CHECK(thread_.IsCurrentThread());

  base::span<const int32_t> fps_range =
      request->GetMetadata<int32_t>(ANDROID_CONTROL_AE_TARGET_FPS_RANGE);
  if (!fps_range.empty()) {
    expected_fps_ = std::max(expected_fps_, fps_range[0]);
  }

  CaptureContext* ctx = CreateCaptureContext(request->frame_number());
  if (!ctx) {
    return false;
  }

  for (auto& b : request->AcquireOutputBuffers()) {
    if (b.stream() == blob_stream_) {
      ctx->has_blob_request = true;
    }
    request->AppendOutputBuffer(std::move(b));
  }

  ctx->num_pending_buffers = request->num_output_buffers();

  return true;
}

bool FrameDropMonitorStreamManipulator::ProcessCaptureResultOnThread(
    Camera3CaptureDescriptor* result) {
  CHECK(thread_.IsCurrentThread());

  if (runtime_options_->sw_privacy_switch_state() ==
      mojom::CameraPrivacySwitchState::ON) {
    return true;
  }

  CaptureContext* ctx = GetCaptureContext(result->frame_number());
  if (!ctx) {
    return true;
  }

  DCHECK_GE(ctx->num_pending_buffers, result->num_output_buffers());
  ctx->num_pending_buffers -= result->num_output_buffers();
  ctx->metadata_received |= result->partial_result() == partial_result_count_;

  base::ScopedClosureRunner ctx_deleter;
  if (ctx->num_pending_buffers == 0 && ctx->metadata_received) {
    ctx_deleter.ReplaceClosure(
        base::BindOnce(&FrameDropMonitorStreamManipulator::RemoveCaptureContext,
                       base::Unretained(this), result->frame_number()));
  }

  // Record feature states for this frame.
  FeatureStates feature_states = {
      .auto_framing_enabled =
          auto_framing_supported_ && runtime_options_->auto_framing_state() !=
                                         mojom::CameraAutoFramingState::OFF,
      .effects_enabled =
          effects_supported_ &&
          HasEnabledEffects(runtime_options_->GetEffectsConfig()),
  };

  FeatureCombination current_combination = FeatureCombination::kNone;
  if (feature_states.auto_framing_enabled && feature_states.effects_enabled) {
    current_combination = FeatureCombination::kAutoFramingAndEffects;
  } else if (feature_states.auto_framing_enabled) {
    current_combination = FeatureCombination::kAutoFraming;
  } else if (feature_states.effects_enabled) {
    current_combination = FeatureCombination::kEffects;
  }

  std::optional<int64_t> current_timestamp = TryGetSensorTimestamp(result);
  if (!current_timestamp) {
    return true;
  }

  // Skip first frame for still capture request.
  if (ctx->has_blob_request) {
    last_timestamp_ = *current_timestamp;
    return true;
  }

  // Skip first frame after a feature setting change.
  if (last_feature_states_.has_value() &&
      current_combination != last_feature_states_.value()) {
    last_feature_states_ = current_combination;
    last_timestamp_ = *current_timestamp;
    return true;
  }

  // Calculate dropped frames by comparing the actual time difference between
  // frames to the expected time difference based on the desired FPS.
  if (last_timestamp_) {
    int64_t actual_time_diff = *current_timestamp - last_timestamp_;
    int64_t expected_time_diff = kNanoSecondsPerSecond / expected_fps_;

    // Adaptive tolerance based on expected frame time (Set 25% of frame time as
    // the default).
    int64_t frame_drop_tolerance = expected_time_diff / 4;

    // If the actual time difference significantly exceeds the expected time
    // difference (plus the tolerance), consider it a dropped frame.
    if (actual_time_diff > expected_time_diff + frame_drop_tolerance) {
      ++metrics_.total_dropped_frames;

      // Increment per-feature dropped frames based on enabled features.
      if (feature_states.auto_framing_enabled) {
        ++metrics_.auto_framing_dropped_frames;
      }
      if (feature_states.effects_enabled) {
        ++metrics_.effects_dropped_frames;
      }
      if (hdrnet_supported_) {
        ++metrics_.hdrnet_dropped_frames;
      }
      if (!feature_states.effects_enabled &&
          !feature_states.auto_framing_enabled && !hdrnet_supported_) {
        ++metrics_.no_effects_dropped_frames;
      }
    }
    total_frames_++;

    if (feature_states.auto_framing_enabled) {
      ++metrics_.auto_framing_total_frames;
    }
    if (feature_states.effects_enabled) {
      ++metrics_.effects_total_frames;
    }
    if (hdrnet_supported_) {
      ++metrics_.hdrnet_total_frames;
    }
    if (!feature_states.effects_enabled &&
        !feature_states.auto_framing_enabled && !hdrnet_supported_) {
      ++metrics_.no_effects_total_frames;
    }

    VLOGF(2) << "Frame Time Diff: " << actual_time_diff
             << " Expected (with tolerance): "
             << expected_time_diff + frame_drop_tolerance
             << " Dropped: " << metrics_.total_dropped_frames;
  }
  last_feature_states_ = current_combination;
  last_timestamp_ = *current_timestamp;

  return true;
}

void FrameDropMonitorStreamManipulator::ResetOnThread() {
  CHECK(thread_.IsCurrentThread());

  expected_fps_ = kMinExpectedFps;
  last_timestamp_ = 0;
  total_frames_ = 0;
  capture_contexts_.clear();

  metrics_ = Metrics{};
}

void FrameDropMonitorStreamManipulator::UploadMetricsOnThread() {
  CHECK(thread_.IsCurrentThread());

  if (total_frames_ == 0) {
    return;
  }

  const int overall_frame_drop_rate =
      metrics_.total_dropped_frames * 100 / total_frames_;

  VLOGF(1) << "Frame Drop Calculation Metrics:" << " overall_frame_drop_rate="
           << overall_frame_drop_rate << "%";

  // Send overall frame drop rate.
  camera_metrics_->SendPipelineFrameDropRate(CameraFeature::kOverall,
                                             overall_frame_drop_rate);

  // Calculate and log per-feature frame drop rates only if the feature was on.
  if (metrics_.auto_framing_total_frames > 0) {
    const int auto_framing_drop_rate = metrics_.auto_framing_dropped_frames *
                                       100 / metrics_.auto_framing_total_frames;
    VLOGF(1) << " auto_framing_drop_rate=" << auto_framing_drop_rate << "%";
    camera_metrics_->SendPipelineFrameDropRate(CameraFeature::kAutoFraming,
                                               auto_framing_drop_rate);
  }
  if (metrics_.effects_total_frames > 0) {
    const int effects_drop_rate =
        metrics_.effects_dropped_frames * 100 / metrics_.effects_total_frames;
    VLOGF(1) << " effects_drop_rate=" << effects_drop_rate << "%";
    camera_metrics_->SendPipelineFrameDropRate(CameraFeature::kEffects,
                                               effects_drop_rate);
  }
  if (metrics_.hdrnet_total_frames > 0) {
    const int hdrnet_drop_rate =
        metrics_.hdrnet_dropped_frames * 100 / metrics_.hdrnet_total_frames;
    VLOGF(1) << " hdrnet_drop_rate=" << hdrnet_drop_rate << "%";
    camera_metrics_->SendPipelineFrameDropRate(CameraFeature::kHdrnet,
                                               hdrnet_drop_rate);
  }
  if (metrics_.no_effects_total_frames > 0) {
    const int no_effects_drop_rate = metrics_.no_effects_dropped_frames * 100 /
                                     metrics_.no_effects_total_frames;
    VLOGF(1) << " no_effects_drop_rate=" << no_effects_drop_rate << "%";
    camera_metrics_->SendPipelineFrameDropRate(CameraFeature::kNone,
                                               no_effects_drop_rate);
  }
}

FrameDropMonitorStreamManipulator::CaptureContext*
FrameDropMonitorStreamManipulator::CreateCaptureContext(uint32_t frame_number) {
  DCHECK(!base::Contains(capture_contexts_, frame_number));
  auto [it, is_inserted] = capture_contexts_.insert(
      std::make_pair(frame_number, std::make_unique<CaptureContext>()));
  if (!is_inserted) {
    LOGF(ERROR) << "Multiple captures with same frame number " << frame_number;
    return nullptr;
  }
  return it->second.get();
}

FrameDropMonitorStreamManipulator::CaptureContext*
FrameDropMonitorStreamManipulator::GetCaptureContext(
    uint32_t frame_number) const {
  auto it = capture_contexts_.find(frame_number);
  return it != capture_contexts_.end() ? it->second.get() : nullptr;
}

void FrameDropMonitorStreamManipulator::RemoveCaptureContext(
    uint32_t frame_number) {
  capture_contexts_.erase(frame_number);
}

}  // namespace cros
