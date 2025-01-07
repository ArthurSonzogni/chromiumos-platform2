// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_FEATURES_FRAME_DROP_MONITOR_FRAME_DROP_MONITOR_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_FRAME_DROP_MONITOR_FRAME_DROP_MONITOR_STREAM_MANIPULATOR_H_

#include <cstdint>
#include <map>
#include <memory>

#include "common/stream_manipulator.h"
#include "cros-camera/camera_metrics.h"
#include "cros-camera/camera_thread.h"

namespace cros {

class FrameDropMonitorStreamManipulator : public StreamManipulator {
 public:
  explicit FrameDropMonitorStreamManipulator(
      RuntimeOptions* runtime_options,
      bool auto_framing_supported = false,
      bool effects_supported = false,
      bool hdrnet_supported = false);
  ~FrameDropMonitorStreamManipulator() override;

  // Implementations of StreamManipulator.
  bool Initialize(const camera_metadata_t* static_info,
                  StreamManipulator::Callbacks callbacks) override;
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config) override;
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override;
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override;
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override;
  bool ProcessCaptureResult(Camera3CaptureDescriptor result) override;
  void Notify(camera3_notify_msg_t msg) override;
  bool Flush() override;

 private:
  // Represents the state of different camera features.
  struct FeatureStates {
    bool auto_framing_enabled = false;
    bool effects_enabled = false;
  };

  struct CaptureContext {
    uint32_t num_pending_buffers = 0;
    bool metadata_received = false;
    bool has_blob_request = false;
  };

  // Stores frame drop metrics.
  struct Metrics {
    int64_t auto_framing_dropped_frames = 0;
    int64_t auto_framing_total_frames = 0;
    int64_t effects_dropped_frames = 0;
    int64_t effects_total_frames = 0;
    int64_t hdrnet_dropped_frames = 0;
    int64_t hdrnet_total_frames = 0;
    int64_t no_effects_dropped_frames = 0;
    int64_t no_effects_total_frames = 0;
    int64_t total_dropped_frames = 0;
  };

  enum class FeatureCombination {
    kNone,
    kAutoFraming,
    kEffects,
    kAutoFramingAndEffects,
  };
  std::optional<FeatureCombination> last_feature_states_;

  bool InitializeOnThread(const camera_metadata_t* static_info,
                          StreamManipulator::Callbacks callbacks);
  bool ConfigureStreamsOnThread(Camera3StreamConfiguration* stream_config);
  bool OnConfiguredStreamsOnThread(Camera3StreamConfiguration* stream_config);
  bool ProcessCaptureRequestOnThread(Camera3CaptureDescriptor* request);
  bool ProcessCaptureResultOnThread(Camera3CaptureDescriptor* result);
  void ResetOnThread();
  void UploadMetricsOnThread();

  CaptureContext* CreateCaptureContext(uint32_t frame_number);
  CaptureContext* GetCaptureContext(uint32_t frame_number) const;
  void RemoveCaptureContext(uint32_t frame_number);

  int expected_fps_ = 0;
  int partial_result_count_ = 0;
  int64_t last_timestamp_ = 0;
  int64_t total_frames_ = 0;

  RuntimeOptions* runtime_options_;
  bool auto_framing_supported_ = false;
  bool effects_supported_ = false;
  bool hdrnet_supported_ = false;

  const camera3_stream_t* blob_stream_ = nullptr;
  std::map<uint32_t, std::unique_ptr<CaptureContext>> capture_contexts_;

  std::unique_ptr<CameraMetrics> camera_metrics_;
  StreamManipulator::Callbacks callbacks_;

  CameraThread thread_;
  Metrics metrics_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_FRAME_DROP_MONITOR_FRAME_DROP_MONITOR_STREAM_MANIPULATOR_H_
