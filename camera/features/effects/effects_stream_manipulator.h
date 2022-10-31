/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_EFFECTS_EFFECTS_STREAM_MANIPULATOR_H_
#define CAMERA_FEATURES_EFFECTS_EFFECTS_STREAM_MANIPULATOR_H_

#include "common/stream_manipulator.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/camera_buffer_pool.h"
#include "common/metadata_logger.h"
#include "common/reloadable_config_file.h"
#include "cros-camera/camera_thread.h"
#include "cros-camera/common_types.h"
#include "gpu/image_processor.h"
#include "gpu/shared_image.h"

#include "ml_core/effects_pipeline.h"

namespace cros {

class EffectsStreamManipulator : public StreamManipulator {
 public:
  // TODO(b:242631540) Find permanent location for this file
  static constexpr const char kOverrideEffectsConfigFile[] =
      "/run/camera/effects/effects_config_override.json";

  struct Options {
    // disable/enable StreamManipulator
    bool enable = false;
    // Config structure for configuring the effects library
    EffectsConfig effects_config;
  };

  explicit EffectsStreamManipulator(base::FilePath config_file_path,
                                    RuntimeOptions* runtime_options);
  ~EffectsStreamManipulator() override = default;

  // Implementations of StreamManipulator.
  bool Initialize(const camera_metadata_t* static_info,
                  CaptureResultCallback result_callback) override;
  bool ConfigureStreams(Camera3StreamConfiguration* stream_config) override;
  bool ConfigureStreamsOnThread(Camera3StreamConfiguration* stream_config);
  bool OnConfiguredStreams(Camera3StreamConfiguration* stream_config) override;
  bool ConstructDefaultRequestSettings(
      android::CameraMetadata* default_request_settings, int type) override;
  bool ProcessCaptureRequest(Camera3CaptureDescriptor* request) override;
  bool ProcessCaptureResult(Camera3CaptureDescriptor* result) override;
  bool ProcessCaptureResultOnThread(Camera3CaptureDescriptor* result);
  bool Notify(camera3_notify_msg_t* msg) override;
  bool Flush() override;
  void OnFrameProcessed(int64_t timestamp,
                        const uint8_t* data,
                        uint32_t data_len);

 private:
  void OnOptionsUpdated(const base::Value& json_values);

  void SetEffect(EffectsConfig new_config);
  void GpuSync();
  void CreatePipeline(const base::FilePath& dlc_root_path);

  ReloadableConfigFile config_;
  Options options_;
  RuntimeOptions* runtime_options_;

  EffectsConfig active_runtime_effects_config_ = EffectsConfig();

  camera3_stream_t* yuv_stream_ = nullptr;
  std::unique_ptr<EffectsPipeline> pipeline_;
  void* buffer_ptr_ = nullptr;

  ScopedBufferHandle frame_buffer_;
  SharedImage gpu_image_;
  uint16_t prev_height_ = 0;
  uint16_t prev_width_ = 0;

  std::unique_ptr<EglContext> egl_context_;
  std::unique_ptr<GpuImageProcessor> image_processor_;

  CameraThread thread_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_EFFECTS_EFFECTS_STREAM_MANIPULATOR_H_
