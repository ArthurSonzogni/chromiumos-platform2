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

#undef Status
#include <absl/status/status.h>
#include <base/memory/weak_ptr.h>

#include "base/sequence_checker.h"
#include "base/threading/thread_checker.h"
#include "common/camera_buffer_pool.h"
#include "common/camera_hal3_helpers.h"
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

  // callback used to signal that an effect has taken effect.
  // Once the callback is fired it is guaranteed that all subsequent
  // frames will have the effect applied.
  // TODO(b:263440749): update callback type
  explicit EffectsStreamManipulator(base::FilePath config_file_path,
                                    RuntimeOptions* runtime_options,
                                    void (*callback)(bool) = nullptr);
  ~EffectsStreamManipulator() override = default;

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
  void OnFrameProcessed(int64_t timestamp,
                        GLuint texture,
                        uint32_t width,
                        uint32_t height);

 private:
  void OnOptionsUpdated(const base::Value::Dict& json_values);

  void SetEffect(EffectsConfig new_config);
  bool SetupGlThread();
  bool EnsureImages(buffer_handle_t buffer_handle);
  bool NV12ToRGBA();
  void RGBAToNV12(GLuint texture, uint32_t width, uint32_t height);
  void CreatePipeline(const base::FilePath& dlc_root_path);
  std::optional<int64_t> TryGetSensorTimestamp(Camera3CaptureDescriptor* desc);
  std::optional<Camera3StreamBuffer> SelectEffectsBuffer(
      Camera3CaptureDescriptor& result);

  ReloadableConfigFile config_;
  RuntimeOptions* runtime_options_;
  StreamManipulator::Callbacks callbacks_;

  EffectsConfig active_runtime_effects_config_
      GUARDED_BY_CONTEXT(sequence_checker_) = EffectsConfig();

  std::unique_ptr<EffectsPipeline> pipeline_
      GUARDED_BY_CONTEXT(sequence_checker_);

  // Buffer for input frame converted into RGBA.
  ScopedBufferHandle input_buffer_rgba_ GUARDED_BY_CONTEXT(gl_thread_checker_);

  // SharedImage for |input_buffer_rgba|.
  SharedImage input_image_rgba_ GUARDED_BY_CONTEXT(gl_thread_checker_);

  SharedImage input_image_yuv_ GUARDED_BY_CONTEXT(gl_thread_checker_);
  absl::Status frame_status_ = absl::OkStatus();

  std::unique_ptr<EglContext> egl_context_
      GUARDED_BY_CONTEXT(gl_thread_checker_);
  std::unique_ptr<GpuImageProcessor> image_processor_
      GUARDED_BY_CONTEXT(gl_thread_checker_);

  int64_t timestamp_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;
  int64_t last_timestamp_ GUARDED_BY_CONTEXT(sequence_checker_) = 0;

  CameraThread gl_thread_;
  scoped_refptr<base::SingleThreadTaskRunner> process_thread_;

  bool effects_enabled_ GUARDED_BY_CONTEXT(sequence_checker_) = true;
  void (*set_effect_callback_)(bool);

  SEQUENCE_CHECKER(sequence_checker_);
  THREAD_CHECKER(gl_thread_checker_);
};

}  // namespace cros

#endif  // CAMERA_FEATURES_EFFECTS_EFFECTS_STREAM_MANIPULATOR_H_
