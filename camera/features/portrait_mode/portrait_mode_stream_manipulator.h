// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_FEATURES_PORTRAIT_MODE_PORTRAIT_MODE_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_PORTRAIT_MODE_PORTRAIT_MODE_STREAM_MANIPULATOR_H_

#include "common/stream_manipulator.h"

#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <base/containers/flat_map.h>

#include "common/camera_buffer_pool.h"
#include "common/camera_hal3_helpers.h"
#include "common/still_capture_processor.h"
#include "cros-camera/camera_metrics.h"
#include "cros-camera/camera_mojo_channel_manager_token.h"
#include "features/portrait_mode/portrait_mode_effect.h"
#include "hardware/camera3.h"

namespace cros {

class PortraitModeStreamManipulator : public StreamManipulator {
 public:
  PortraitModeStreamManipulator(
      CameraMojoChannelManagerToken* mojo_manager_token,
      std::unique_ptr<StillCaptureProcessor> still_capture_processor);
  ~PortraitModeStreamManipulator() override;

  static bool UpdateVendorTags(VendorTagManager& vendor_tag_manager);
  static bool UpdateStaticMetadata(android::CameraMetadata* static_info);

  // Implementations of StreamManipulator.
  bool Initialize(const camera_metadata_t* static_info,
                  StreamManipulator::Callbacks callbacks) override;
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config,
                        const StreamEffectMap* stream_effects_map) override;
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override;
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override;
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override;
  bool ProcessCaptureResult(Camera3CaptureDescriptor result) override;
  void Notify(camera3_notify_msg_t msg) override;
  bool Flush() override;

 private:
  struct PortraitModeConfig {
    // The stream configured for Portrait Mode requests. We assume that
    // the effect will not be applied to multiple streams at the same time.
    const camera3_stream_t* stream;

    // Whether Rectiface is enabled.
    bool enable_rectiface;
  };

  struct CaptureContext {
    uint32_t num_pending_buffers = 0;
    uint32_t orientation = 0;
    bool metadata_received = false;
    bool has_pending_blob = false;
    // Will be set to `true` if a human face is detected.
    bool has_portrait_result = false;
    bool has_updated_metadata = false;
    std::optional<CameraBufferPool::Buffer> still_yuv_buffer;
    std::optional<SegmentationResult> segmentation_result;
    // Holds the last partial result if it comes before the buffer returns.
    std::optional<Camera3CaptureDescriptor> pending_result_;
  };

  struct Metrics {
    int num_still_shot_taken = 0;
    int num_portrait_shot_success = 0;
    base::TimeTicks last_process_time_start;
    base::TimeDelta accumulated_process_latency = base::Seconds(0);
    base::flat_map<PortraitModeError, int> errors;
  };

  bool InitializeOnThread(const camera_metadata_t* static_info,
                          StreamManipulator::Callbacks callbacks);
  bool ConfigureStreamsOnThread(Camera3StreamConfiguration* stream_config,
                                const StreamEffectMap* stream_effects_map);
  bool OnConfiguredStreamsOnThread(Camera3StreamConfiguration* stream_config);
  bool ProcessCaptureRequestOnThread(Camera3CaptureDescriptor* request);
  bool ProcessCaptureResultOnThread(Camera3CaptureDescriptor result);
  void ReturnStillCaptureResultOnThread(Camera3CaptureDescriptor result);
  void ResetOnThread();
  void UploadMetricsOnThread();

  bool IsPortraitModeStream(const camera3_stream_t* stream,
                            const StreamEffectMap* stream_effects_map);
  bool IsPortraitModeRequest(const Camera3CaptureDescriptor* request);

  CaptureContext* CreateCaptureContext(uint32_t frame_number);
  CaptureContext* GetCaptureContext(uint32_t frame_number) const;
  void RemoveCaptureContext(uint32_t frame_number);

  CameraMojoChannelManagerToken* mojo_manager_token_;
  std::unique_ptr<StillCaptureProcessor> still_capture_processor_;
  std::unique_ptr<CameraMetrics> camera_metrics_;
  StreamManipulator::Callbacks callbacks_;
  uint32_t partial_result_count_ = 0;

  // PortraitModeEffect instance.
  std::unique_ptr<PortraitModeEffect> portrait_mode_;

  // Fixed after ConfigureStreams().
  std::optional<PortraitModeConfig> portrait_mode_config_;

  // Per-stream-config contexts.
  std::vector<camera3_stream_t*> client_streams_;
  const camera3_stream_t* blob_stream_ = nullptr;
  const camera3_stream_t* portrait_blob_stream_ = nullptr;
  const camera3_stream_t* yuv_stream_for_portrait_blob_ = nullptr;

  // YUV blob stream created by us.
  std::unique_ptr<camera3_stream_t> still_yuv_stream_;

  // Buffers for the YUV blob stream.
  std::unique_ptr<CameraBufferPool> still_yuv_buffer_pool_;

  // Frame number corresponds to in-flight still image capture requests.
  std::map<uint32_t, std::unique_ptr<CaptureContext>> capture_contexts_;

  CameraThread thread_;
  Metrics metrics_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_PORTRAIT_MODE_PORTRAIT_MODE_STREAM_MANIPULATOR_H_
