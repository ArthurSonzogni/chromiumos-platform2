/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_AUTO_FRAMING_AUTO_FRAMING_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_AUTO_FRAMING_AUTO_FRAMING_STREAM_MANIPULATOR_H_

#include "common/stream_manipulator.h"

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <base/time/time.h>
#include <base/timer/elapsed_timer.h>

#include "common/camera_buffer_pool.h"
#include "common/metadata_logger.h"
#include "common/reloadable_config_file.h"
#include "cros-camera/camera_thread.h"
#include "cros-camera/common_types.h"
#include "features/auto_framing/auto_framing_client.h"
#include "features/auto_framing/face_tracker.h"
#include "features/auto_framing/framer.h"
#include "gpu/egl/egl_context.h"
#include "gpu/gpu_resources.h"
#include "gpu/image_processor.h"

namespace cros {

class AutoFramingStreamManipulator : public StreamManipulator {
 public:
  // The auto framing config file that overrides the default one for debugging.
  // The file should contain a JSON map for the Options defined below.
  static constexpr const char kOverrideAutoFramingConfigFile[] =
      "/run/camera/auto_framing_config.json";

  enum class Detector {
    // Face detector. It cannot be paired with MotionModel::kLibAutoFraming.
    kFace = 0,
    // Face-Person-Pose detector. The output ROI contains face and part of body
    // regions.
    kFacePersonPose = 1,
  };

  enum class MotionModel {
    // IIR filtering implemented in Framer.
    kIirFilter = 0,
    // Motion model implemented in libautoframing.
    kLibAutoFraming = 1,
  };

  struct Options {
    // The detection model for detecting regions of interest.
    Detector detector = Detector::kFacePersonPose;

    // The motion model for smoothing framing window moves.
    MotionModel motion_model = MotionModel::kLibAutoFraming;

    // The filtering algorithm to scale the cropped region into output frames.
    FilterMode output_filter_mode = FilterMode::kBicubic;

    // Number of frames per second to run detection. The actual rate is capped
    // by the detection speed. Setting zero means one-shot detection for each
    // auto-framing enabled period.
    float detection_rate = 0.0f;

    // Delay when enabling auto-framing.
    base::TimeDelta enable_delay = base::Seconds(0.5);

    // Delay when disabling auto-framing.
    base::TimeDelta disable_delay = base::Seconds(0.5);

    // Whether the CrOS Auto Framing is enabled.
    std::optional<bool> enable;

    // Whether to enable debug mode. In debug mode the frame is not cropped.
    // Instead the ROIs and active crop area is piggybacked in the
    // FACE_RECTANGLES metadata and we can use Chrome Camera App to visualize
    // the auto-framing transition.
    bool debug = false;
  };

  explicit AutoFramingStreamManipulator(RuntimeOptions* runtime_options,
                                        const Options& options,
                                        base::FilePath config_file_path);
  ~AutoFramingStreamManipulator() override;

  // Implementations of StreamManipulator.
  bool Initialize(GpuResources* gpu_resources_,
                  const camera_metadata_t* static_info,
                  CaptureResultCallback result_callback) override;
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config) override;
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override;
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override;
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override;
  bool ProcessCaptureResult(Camera3CaptureDescriptor* result) override;
  bool Notify(camera3_notify_msg_t* msg) override;
  bool Flush() override;

  // For testing.
  const Rect<float>& region_of_interest() const { return region_of_interest_; }
  const Rect<float>& active_crop_region() const { return active_crop_region_; }

 private:
  struct CaptureContext;

  enum class State {
    // The state when auto-framing is turned off. Settles the crop window to the
    // full image.
    kOff,
    // The intermediate state before transitioning to |kOn| state.
    kTransitionToOn,
    // The state when auto-framing is turned on. Moves the crop window
    // continuously based on the detection rate.
    kOn,
    // The intermediate state before transitioning to |kOff| state.
    kTransitionToOff,
  };

  bool InitializeOnThread(const camera_metadata_t* static_info,
                          CaptureResultCallback result_callback);
  bool ConfigureStreamsOnThread(Camera3StreamConfiguration* stream_config);
  bool OnConfiguredStreamsOnThread(Camera3StreamConfiguration* stream_config);
  bool ProcessCaptureRequestOnThread(Camera3CaptureDescriptor* request);
  bool ProcessCaptureResultOnThread(Camera3CaptureDescriptor* result);

  bool SetUpPipelineOnThread(uint32_t target_aspect_ratio_x,
                             uint32_t target_aspect_ratio_y);
  void UpdateFaceRectangleMetadataOnThread(Camera3CaptureDescriptor* result);
  void HandleFramingErrorOnThread(Camera3CaptureDescriptor* result);
  void ResetOnThread();
  void UpdateOptionsOnThread(const base::Value& json_values);
  std::pair<State, State> StateTransitionOnThread();

  void OnOptionsUpdated(const base::Value& json_values);

  CaptureContext* CreateCaptureContext(uint32_t frame_number);
  CaptureContext* GetCaptureContext(uint32_t frame_number) const;
  void RemoveCaptureContext(uint32_t frame_number);

  // Crops |input_yuv| into |output_yuv| with the |crop_region|.
  base::ScopedFD CropBufferOnThread(buffer_handle_t input_yuv,
                                    base::ScopedFD input_fence,
                                    buffer_handle_t output_yuv,
                                    base::ScopedFD output_fence,
                                    const Rect<float>& crop_region);

  bool GetEnabled() const;

  ReloadableConfigFile config_;

  Options options_;

  std::unique_ptr<EglContext> egl_context_;
  std::unique_ptr<GpuImageProcessor> image_processor_;

  RuntimeOptions* runtime_options_;

  // Determined by static camera metadata and fixed after Initialize().
  Size active_array_dimension_;
  Size full_frame_size_;
  Rect<float> full_frame_crop_;
  int partial_result_count_ = 0;

  // Per-stream-config contexts.
  State state_ = State::kOff;
  base::ElapsedTimer state_transition_timer_;
  std::vector<camera3_stream_t*> client_streams_;
  camera3_stream_t full_frame_stream_ = {};
  const camera3_stream_t* target_output_stream_ = nullptr;
  bool override_crop_window_ = false;
  std::map<uint32_t, std::unique_ptr<CaptureContext>> capture_contexts_;
  int64_t last_timestamp_ = 0;
  int64_t timestamp_offset_ = 0;

  AutoFramingClient auto_framing_client_;
  std::unique_ptr<FaceTracker> face_tracker_;
  std::unique_ptr<Framer> framer_;
  std::unique_ptr<CameraBufferPool> full_frame_buffer_pool_;

  std::vector<Rect<float>> faces_;
  Rect<float> region_of_interest_ = {0.0f, 0.0f, 1.0f, 1.0f};
  Rect<float> active_crop_region_ = {0.0f, 0.0f, 1.0f, 1.0f};

  // Metadata logger for tests and debugging.
  MetadataLogger metadata_logger_;

  CameraThread thread_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_AUTO_FRAMING_AUTO_FRAMING_STREAM_MANIPULATOR_H_
