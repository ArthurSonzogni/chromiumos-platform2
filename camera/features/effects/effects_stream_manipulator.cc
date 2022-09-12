/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/effects/effects_stream_manipulator.h"

#include <sync/sync.h>

#include <unistd.h>
#include <algorithm>
#include <numeric>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include "base/containers/stack_container.h"
#include <base/callback_helpers.h>
#include <base/time/time.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metadata_utils.h"
#include "gpu/egl/egl_fence.h"
#include "gpu/gles/texture_2d.h"
#include "ml_core/mojo/effects_pipeline.mojom.h"

namespace cros {

namespace {

bool GetStringFromKey(const base::Value& obj,
                      const std::string& key,
                      std::string* value) {
  const std::string* val = obj.FindStringKey(key);
  if (!val || val->empty()) {
    return false;
  }

  *value = *val;
  return true;
}

constexpr char kEffectKey[] = "effect";
constexpr char kBlurScaleKey[] = "blur_scale";
constexpr char kBlurSamplesKey[] = "blur_samples";

constexpr uint8_t kMaxNumBuffers = 8;

constexpr uint32_t kRGBAFormat = HAL_PIXEL_FORMAT_RGBX_8888;
constexpr uint32_t kBufferUsage = GRALLOC_USAGE_SW_READ_OFTEN |
                                  GRALLOC_USAGE_SW_WRITE_OFTEN |
                                  GRALLOC_USAGE_HW_TEXTURE;

class RenderedImageObserver : public ProcessedFrameObserver {
 public:
  explicit RenderedImageObserver(
      base::RepeatingCallback<void(int64_t, const uint8_t*, uint32_t)>
          processed_frame_callback)
      : frame_processed_callback_(processed_frame_callback) {}
  virtual void OnFrameProcessed(int64_t timestamp,
                                const uint8_t* frame_data,
                                uint32_t frame_width,
                                uint32_t frame_height,
                                uint32_t stride) {
    frame_processed_callback_.Run(timestamp, frame_data, stride * frame_height);
  }

 private:
  base::RepeatingCallback<void(int64_t, const uint8_t*, uint32_t)>
      frame_processed_callback_;
};

}  // namespace

EffectsStreamManipulator::EffectsStreamManipulator(
    base::FilePath config_file_path, const RuntimeOptions* runtime_options)
    : config_(ReloadableConfigFile::Options{
          config_file_path, base::FilePath(kOverrideEffectsConfigFile)}),
      runtime_options_(runtime_options),
      thread_("EffectsThread") {
  if (!config_.IsValid()) {
    LOGF(ERROR) << "Cannot load valid config; turn off feature by default";
    options_.enable = false;
  }
  config_.SetCallback(base::BindRepeating(
      &EffectsStreamManipulator::OnOptionsUpdated, base::Unretained(this)));
  CHECK(thread_.Start());
}

bool EffectsStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    CaptureResultCallback result_callback) {
  return true;
}

bool EffectsStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  bool ret;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&EffectsStreamManipulator::ConfigureStreamsOnThread,
                     base::Unretained(this), stream_config),
      &ret);
  return ret;
}

bool EffectsStreamManipulator::ConfigureStreamsOnThread(
    Camera3StreamConfiguration* stream_config) {
  yuv_stream_ = nullptr;

  for (auto* s : stream_config->GetStreams()) {
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
      if ((!yuv_stream_ || s->width > yuv_stream_->width) &&
          ((s->usage & GRALLOC_USAGE_HW_COMPOSER) ==
           GRALLOC_USAGE_HW_COMPOSER)) {
        yuv_stream_ = s;
      }
    }
  }

  if (yuv_stream_) {
    LOGF(INFO) << "YUV stream for EffectsStreamManipulator: "
               << GetDebugString(yuv_stream_);
  } else {
    LOGF(ERROR) << "No YUV stream for EffectsStreamManipulator.";
    return false;
  }
  yuv_stream_->usage |= kBufferUsage;

  pipeline_ = EffectsPipeline::Create();
  pipeline_->SetRenderedImageObserver(std::make_unique<RenderedImageObserver>(
      base::BindRepeating(&EffectsStreamManipulator::OnFrameProcessed,
                          base::Unretained(this))));

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

bool EffectsStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor* result) {
  bool ret;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&EffectsStreamManipulator::ProcessCaptureResultOnThread,
                     base::Unretained(this), result),
      &ret);
  return ret;
}

bool EffectsStreamManipulator::ProcessCaptureResultOnThread(
    Camera3CaptureDescriptor* result) {
  camera3_stream_buffer_t yuv_buffer = {};
  base::StackVector<camera3_stream_buffer_t, kMaxNumBuffers> result_buffers;
  for (auto& b : result->GetOutputBuffers()) {
    if (b.stream != yuv_stream_) {
      result_buffers->push_back(b);
    } else {
      yuv_buffer = b;
    }
  }
  if (yuv_buffer.status != CAMERA3_BUFFER_STATUS_OK) {
    LOGF(ERROR) << "EffectsStreamManipulator received buffer with "
                   "error in result "
                << result->frame_number();
    return false;
  }

  buffer_handle_t buffer_handle = *yuv_buffer.buffer;
  uint32_t width = CameraBufferManager::GetWidth(buffer_handle);
  uint32_t height = CameraBufferManager::GetHeight(buffer_handle);

  auto manager = CameraBufferManager::GetInstance();
  manager->Register(buffer_handle);

  SharedImage input_image = SharedImage::CreateFromBuffer(
      buffer_handle, Texture2D::Target::kTarget2D, true);

  if (width != prev_width_ || height != prev_height_) {
    frame_buffer_ = CameraBufferManager::AllocateScopedBuffer(
        width, height, kRGBAFormat, kBufferUsage);
    if (!frame_buffer_) {
      LOGF(ERROR) << "Failed to allocate frame buffer";
      return false;
    }
    gpu_image_ = SharedImage::CreateFromBuffer(*frame_buffer_,
                                               Texture2D::Target::kTarget2D);
  }
  prev_height_ = height;
  prev_width_ = width;

  bool conv_result = image_processor_->NV12ToRGBA(
      input_image.y_texture(), input_image.uv_texture(), gpu_image_.texture());
  glFinish();

  if (!conv_result) {
    LOGF(ERROR) << "Failed to convert from YUV to RGB";
    return false;
  }

  ScopedMapping scoped_mapping = ScopedMapping(gpu_image_.buffer());
  buffer_ptr_ = scoped_mapping.plane(0).addr;

  auto new_config = GetRuntimeOptionsEffectsConfig();
  if (active_runtime_effects_config_ != new_config) {
    active_runtime_effects_config_ = new_config;
    SetEffect(new_config);
  }
  pipeline_->ProcessFrame(result->frame_number(),
                          reinterpret_cast<const uint8_t*>(buffer_ptr_),
                          scoped_mapping.width(), scoped_mapping.height(),
                          scoped_mapping.plane(0).stride);

  pipeline_->Wait();

  conv_result = image_processor_->RGBAToNV12(
      gpu_image_.texture(), input_image.y_texture(), input_image.uv_texture());
  glFinish();

  if (!conv_result) {
    LOGF(ERROR) << "Failed to convert from RGB to YUV";
    return false;
  }

  manager->Deregister(buffer_handle);
  yuv_buffer.status = CAMERA3_BUFFER_STATUS_OK;
  result_buffers->push_back(yuv_buffer);
  result->SetOutputBuffers(base::span<camera3_stream_buffer_t>(
      result_buffers->begin(), result_buffers->end()));
  return true;
}

bool EffectsStreamManipulator::Notify(camera3_notify_msg_t* msg) {
  return true;
}

bool EffectsStreamManipulator::Flush() {
  return true;
}

void EffectsStreamManipulator::OnFrameProcessed(int64_t timestamp,
                                                const uint8_t* data,
                                                uint32_t data_len) {
  memcpy(buffer_ptr_, data, data_len);
}

void EffectsStreamManipulator::OnOptionsUpdated(
    const base::Value& json_values) {
  if (!pipeline_) {
    return;
  }
  EffectsConfig new_config;
  std::string tmp;
  if (GetStringFromKey(json_values, kEffectKey, &tmp)) {
    if (tmp == std::string("blur")) {
      new_config.effect = mojom::CameraEffect::BACKGROUND_BLUR;
      LoadIfExist(json_values, kBlurScaleKey, &new_config.blur_scale);
      int blur_samples_tmp;
      LoadIfExist(json_values, kBlurSamplesKey, &blur_samples_tmp);
      new_config.blur_samples = static_cast<uint8_t>(blur_samples_tmp);
    } else if (tmp == std::string("replace")) {
      new_config.effect = mojom::CameraEffect::BACKGROUND_REPLACE;
    } else if (tmp == std::string("relight")) {
      new_config.effect = mojom::CameraEffect::PORTRAIT_RELIGHT;
    } else if (tmp == std::string("none")) {
      new_config.effect = mojom::CameraEffect::NONE;
    } else {
      LOGF(WARNING) << "Unknown Effect: " << tmp;
      return;
    }
    LOGF(INFO) << "Effect Updated: " << tmp;

    pipeline_->SetEffect(&new_config, nullptr);
  }
}

EffectsConfig EffectsStreamManipulator::GetRuntimeOptionsEffectsConfig() {
  EffectsConfig new_config;
  new_config.effect = runtime_options_->effects_config->effect;
  new_config.blur_scale = runtime_options_->effects_config->blur_scale;
  new_config.blur_samples = runtime_options_->effects_config->blur_samples;
  new_config.segmentation_gpu_api =
      runtime_options_->effects_config->segmentation_gpu_api;
  new_config.graph_max_frames_in_flight =
      runtime_options_->effects_config->graph_max_frames_in_flight;
  return new_config;
}

void EffectsStreamManipulator::SetEffect(EffectsConfig new_config) {
  pipeline_->SetEffect(&new_config, nullptr);
}

}  // namespace cros
