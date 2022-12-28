/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/effects/effects_stream_manipulator.h"

#include <GLES3/gl3.h>

#include <hardware/camera3.h>
#include <sync/sync.h>

#include <unistd.h>
#include <algorithm>
#include <numeric>
#include <optional>
#include <string>
#include <utility>

#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/time/time.h>
#include <base/containers/stack_container.h>
#include <base/functional/callback_helpers.h>

#include "common/camera_hal3_helpers.h"
#include "common/stream_manipulator.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metadata_utils.h"
#include "gpu/egl/egl_fence.h"
#include "gpu/gles/texture_2d.h"
#include "ml_core/mojo/effects_pipeline.mojom.h"

namespace cros {

namespace {

const int kSyncWaitTimeoutMs = 8;

bool GetStringFromKey(const base::Value::Dict& obj,
                      const std::string& key,
                      std::string* value) {
  const std::string* val = obj.FindString(key);
  if (!val || val->empty()) {
    return false;
  }

  *value = *val;
  return true;
}

constexpr char kEffectKey[] = "effect";
constexpr char kBlurLevelKey[] = "blur_level";
constexpr char kBlurEnabled[] = "blur_enabled";
constexpr char kReplaceEnabled[] = "replace_enabled";
constexpr char kRelightEnabled[] = "relight_enabled";

constexpr uint32_t kRGBAFormat = HAL_PIXEL_FORMAT_RGBX_8888;
constexpr uint32_t kBufferUsage = GRALLOC_USAGE_HW_TEXTURE;

class RenderedImageObserver : public ProcessedFrameObserver {
 public:
  explicit RenderedImageObserver(
      base::RepeatingCallback<void(int64_t, GLuint, uint32_t, uint32_t)>
          processed_frame_callback)
      : frame_processed_callback_(processed_frame_callback) {}
  void OnFrameProcessed(int64_t timestamp,
                        GLuint frame_texture,
                        uint32_t frame_width,
                        uint32_t frame_height) override {
    frame_processed_callback_.Run(timestamp, frame_texture, frame_width,
                                  frame_height);
  }

 private:
  base::RepeatingCallback<void(int64_t, GLuint, uint32_t, uint32_t)>
      frame_processed_callback_;
};

}  // namespace

EffectsStreamManipulator::EffectsStreamManipulator(
    base::FilePath config_file_path,
    RuntimeOptions* runtime_options,
    void (*callback)(bool))
    : config_(ReloadableConfigFile::Options{
          config_file_path, base::FilePath(kOverrideEffectsConfigFile)}),
      runtime_options_(runtime_options),
      gl_thread_("EffectsGlThread"),
      set_effect_callback_(callback) {
  if (!config_.IsValid()) {
    LOGF(ERROR) << "Cannot load valid config. Turning off feature by default";
    options_.enable = false;
  }

  CHECK(gl_thread_.Start());

  bool ret;
  gl_thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&EffectsStreamManipulator::SetupGlThread,
                     base::Unretained(this)),
      &ret);
  if (!ret) {
    LOGF(ERROR) << "Failed to start GL thread. Turning off feature by default";
    options_.enable = false;
  }

  if (!runtime_options_->GetDlcRootPath().empty()) {
    CreatePipeline(base::FilePath(runtime_options_->GetDlcRootPath()));
  }
}

bool EffectsStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  callbacks_ = std::move(callbacks);
  return true;
}

bool EffectsStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  return true;
}

bool EffectsStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  return true;
}

bool EffectsStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  return true;
}

bool EffectsStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  return true;
}

std::optional<int64_t> EffectsStreamManipulator::TryGetSensorTimestamp(
    Camera3CaptureDescriptor* desc) {
  base::span<const int64_t> timestamp =
      desc->GetMetadata<int64_t>(ANDROID_SENSOR_TIMESTAMP / 1000);
  return timestamp.size() == 1 ? std::make_optional(timestamp[0])
                               : std::nullopt;
}

std::optional<Camera3StreamBuffer>
EffectsStreamManipulator::SelectEffectsBuffer(
    Camera3CaptureDescriptor& result) {
  // This removes all the output buffers in the capture result and picks one
  // buffer suitable for effects processing. This works only if there's only one
  // output buffer in the capture result.
  std::vector<Camera3StreamBuffer> output_buffers =
      result.AcquireOutputBuffers();
  Camera3StreamBuffer* ret = nullptr;

  for (auto& b : output_buffers) {
    const auto* s = b.stream();
    if (s->stream_type != CAMERA3_STREAM_OUTPUT) {
      continue;
    }

    if (s->format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
        s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
      if (s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED &&
          (s->usage & GRALLOC_USAGE_HW_CAMERA_ZSL) ==
              GRALLOC_USAGE_HW_CAMERA_ZSL) {
        // Ignore ZSL streams.
        continue;
      }

      // TODO(b/244518466) Figure out how to handle many streams
      // Currently selecting the widest stream to process as it satisfies
      // the most initial VC situations. Want to expand this to handle
      // many streams
      if (!ret || s->width >= ret->stream()->width) {
        ret = &b;
      }
    }
  }

  if (!ret) {
    result.SetOutputBuffers(std::move(output_buffers));
    return std::nullopt;
  }
  return std::make_optional(std::move(*ret));
}

bool EffectsStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor result) {
  base::ScopedClosureRunner callback_action =
      StreamManipulator::MakeScopedCaptureResultCallbackRunner(
          callbacks_.result_callback, result);
  if (runtime_options_->sw_privacy_switch_state() ==
      mojom::CameraPrivacySwitchState::ON) {
    return true;
  }

  if (!pipeline_ && !runtime_options_->GetDlcRootPath().empty()) {
    CreatePipeline(base::FilePath(runtime_options_->GetDlcRootPath()));
  }
  if (!pipeline_)
    return true;

  auto new_config = runtime_options_->GetEffectsConfig();
  if (active_runtime_effects_config_ != new_config) {
    active_runtime_effects_config_ = new_config;
    SetEffect(&new_config);
  }

  if (!effects_enabled_) {
    return true;
  }

  std::optional<Camera3StreamBuffer> result_buffer =
      SelectEffectsBuffer(result);
  if (!result_buffer.has_value())
    return true;

  if (result_buffer->status() != CAMERA3_BUFFER_STATUS_OK) {
    LOGF(ERROR) << "EffectsStreamManipulator received buffer with "
                   "error in result "
                << result.frame_number();
    return false;
  }

  if (!result_buffer->WaitOnAndClearReleaseFence(kSyncWaitTimeoutMs)) {
    LOGF(ERROR) << "Timed out waiting for input buffer";
    return false;
  }

  buffer_handle_t buffer_handle = *result_buffer->buffer();

  auto manager = CameraBufferManager::GetInstance();
  if (manager->Register(buffer_handle) != 0) {
    LOG(ERROR) << "Failed to register buffer";
    return false;
  }
  base::ScopedClosureRunner deregister_action(base::BindOnce(
      [](CameraBufferManager* manager, buffer_handle_t buffer_handle) {
        if (manager->Deregister(buffer_handle) != 0) {
          LOG(ERROR) << "Failed to deregister buffer";
        }
      },
      manager, buffer_handle));

  bool ret;

  gl_thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&EffectsStreamManipulator::EnsureImages,
                     base::Unretained(this), buffer_handle),
      &ret);
  if (!ret) {
    LOGF(ERROR) << "Failed to ensure GPU resources";
    return false;
  }

  gl_thread_.PostTaskSync(FROM_HERE,
                          base::BindOnce(&EffectsStreamManipulator::NV12ToRGBA,
                                         base::Unretained(this)),
                          &ret);
  if (!ret) {
    LOGF(ERROR) << "Failed to convert from YUV to RGB";
    return false;
  }

  auto timestamp = TryGetSensorTimestamp(&result);
  timestamp_ = timestamp.has_value() ? *timestamp : timestamp_ + 1;

  if (timestamp_ <= last_timestamp_) {
    uint64_t timestamp_offset = last_timestamp_ + 1 - timestamp_;
    timestamp_ += timestamp_offset;
    LOGF(INFO) << "Found out of order timestamp."
                  "Increasing timestamp to "
               << timestamp_;
  }
  last_timestamp_ = timestamp_;
  frame_status_ = absl::OkStatus();
  pipeline_->ProcessFrame(timestamp_, input_image_rgba_.texture().handle(),
                          input_image_rgba_.texture().width(),
                          input_image_rgba_.texture().height());
  pipeline_->Wait();
  if (!frame_status_.ok()) {
    LOG(ERROR) << frame_status_.message();
    return false;
  }

  result_buffer->mutable_raw_buffer().status = CAMERA3_BUFFER_STATUS_OK;
  result.AppendOutputBuffer(std::move(result_buffer.value()));

  return true;
}

void EffectsStreamManipulator::Notify(camera3_notify_msg_t msg) {
  callbacks_.notify_callback.Run(std::move(msg));
}

bool EffectsStreamManipulator::Flush() {
  return true;
}

void EffectsStreamManipulator::OnFrameProcessed(int64_t timestamp,
                                                GLuint texture,
                                                uint32_t width,
                                                uint32_t height) {
  gl_thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&EffectsStreamManipulator::RGBAToNV12,
                     base::Unretained(this), texture, width, height));
}

void EffectsStreamManipulator::OnOptionsUpdated(
    const base::Value::Dict& json_values) {
  if (!pipeline_) {
    LOGF(WARNING) << "OnOptionsUpdated called, but pipeline not ready.";
    return;
  }
  LOGF(INFO) << "Reloadable Options update detected";
  EffectsConfig new_config;
  std::string tmp;
  if (GetStringFromKey(json_values, kEffectKey, &tmp)) {
    if (tmp == std::string("blur")) {
      new_config.effect = mojom::CameraEffect::kBackgroundBlur;
      new_config.blur_enabled = true;
    } else if (tmp == std::string("replace")) {
      new_config.effect = mojom::CameraEffect::kBackgroundReplace;
      new_config.replace_enabled = true;
    } else if (tmp == std::string("relight")) {
      new_config.effect = mojom::CameraEffect::kPortraitRelight;
      new_config.relight_enabled = true;
    } else if (tmp == std::string("blur_relight")) {
      new_config.effect = mojom::CameraEffect::kBackgroundBlurPortraitRelight;
      new_config.blur_enabled = true;
      new_config.relight_enabled = true;
    } else if (tmp == std::string("none")) {
      new_config.effect = mojom::CameraEffect::kNone;
    } else {
      LOGF(WARNING) << "Unknown Effect: " << tmp;
      return;
    }
  }
  LoadIfExist(json_values, kBlurEnabled, &new_config.blur_enabled);
  LoadIfExist(json_values, kReplaceEnabled, &new_config.replace_enabled);
  LoadIfExist(json_values, kRelightEnabled, &new_config.relight_enabled);

  std::string blur_level;
  if (GetStringFromKey(json_values, kBlurLevelKey, &blur_level)) {
    if (blur_level == "lowest") {
      new_config.blur_level = mojom::BlurLevel::kLowest;
    } else if (blur_level == "light") {
      new_config.blur_level = mojom::BlurLevel::kLight;
    } else if (blur_level == "medium") {
      new_config.blur_level = mojom::BlurLevel::kMedium;
    } else if (blur_level == "heavy") {
      new_config.blur_level = mojom::BlurLevel::kHeavy;
    } else if (blur_level == "maximum") {
      new_config.blur_level = mojom::BlurLevel::kMaximum;
    }
  }
  LOGF(INFO) << "Effect Updated: " << tmp;
  SetEffect(&new_config);
}

void EffectsStreamManipulator::SetEffect(EffectsConfig* new_config) {
  if (pipeline_) {
    pipeline_->SetEffect(new_config, set_effect_callback_);
    effects_enabled_ = new_config->HasEnabledEffects();
  } else {
    LOGF(WARNING) << "SetEffect called, but pipeline not ready.";
  }
}

bool EffectsStreamManipulator::SetupGlThread() {
  DCHECK(gl_thread_.task_runner()->BelongsToCurrentThread());
  if (!egl_context_) {
    egl_context_ = EglContext::GetSurfacelessContext();
    if (!egl_context_->IsValid()) {
      LOGF(ERROR) << "Failed to create EGL context";
      return false;
    }
  }
  if (!egl_context_->MakeCurrent()) {
    LOGF(ERROR) << "Failed to make EGL context current";
    return false;
  }

  image_processor_ = std::make_unique<GpuImageProcessor>();
  if (!image_processor_) {
    LOGF(ERROR) << "Failed to create GpuImageProcessor";
    return false;
  }

  return true;
}

bool EffectsStreamManipulator::EnsureImages(buffer_handle_t buffer_handle) {
  DCHECK(gl_thread_.task_runner()->BelongsToCurrentThread());

  uint32_t target_width = CameraBufferManager::GetWidth(buffer_handle);
  uint32_t target_height = CameraBufferManager::GetHeight(buffer_handle);

  input_image_yuv_ = SharedImage::CreateFromBuffer(
      buffer_handle, Texture2D::Target::kTarget2D, true);

  if (!input_buffer_rgba_ ||
      target_width != CameraBufferManager::GetWidth(*input_buffer_rgba_) ||
      target_height != CameraBufferManager::GetHeight(*input_buffer_rgba_)) {
    input_buffer_rgba_ = CameraBufferManager::AllocateScopedBuffer(
        target_width, target_height, kRGBAFormat,
        kBufferUsage | GRALLOC_USAGE_SW_READ_NEVER);
    if (!input_buffer_rgba_) {
      LOGF(ERROR) << "Failed to allocate frame buffer";
      return false;
    }
    input_image_rgba_ = SharedImage::CreateFromBuffer(
        *input_buffer_rgba_, Texture2D::Target::kTarget2D);
  }

  return true;
}

bool EffectsStreamManipulator::NV12ToRGBA() {
  DCHECK(gl_thread_.task_runner()->BelongsToCurrentThread());

  bool conv_result = image_processor_->NV12ToRGBA(input_image_yuv_.y_texture(),
                                                  input_image_yuv_.uv_texture(),
                                                  input_image_rgba_.texture());
  glFinish();
  return conv_result;
}

void EffectsStreamManipulator::RGBAToNV12(GLuint texture,
                                          uint32_t width,
                                          uint32_t height) {
  DCHECK(gl_thread_.task_runner()->BelongsToCurrentThread());

  Texture2D texture_2d(texture, kRGBAFormat, width, height);
  bool conv_result = image_processor_->RGBAToNV12(
      texture_2d, input_image_yuv_.y_texture(), input_image_yuv_.uv_texture());
  glFinish();
  texture_2d.Release();

  input_image_yuv_ = SharedImage();

  if (!conv_result) {
    frame_status_ = absl::InternalError("Failed to convert from RGB to YUV");
  }
}

void EffectsStreamManipulator::CreatePipeline(
    const base::FilePath& dlc_root_path) {
  pipeline_ = EffectsPipeline::Create(dlc_root_path, egl_context_->Get());
  pipeline_->SetRenderedImageObserver(std::make_unique<RenderedImageObserver>(
      base::BindRepeating(&EffectsStreamManipulator::OnFrameProcessed,
                          base::Unretained(this))));
  config_.SetCallback(base::BindRepeating(
      &EffectsStreamManipulator::OnOptionsUpdated, base::Unretained(this)));
}

}  // namespace cros
