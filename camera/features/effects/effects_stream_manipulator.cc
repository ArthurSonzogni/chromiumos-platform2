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
#include <deque>
#include <numeric>
#include <optional>
#include <string>
#include <utility>

#include <base/containers/stack_container.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/callback_helpers.h>
#include <base/no_destructor.h>
#include <base/time/time.h>

#include "camera/mojo/effects/effects_pipeline.mojom.h"
#include "common/camera_hal3_helpers.h"
#include "common/stream_manipulator.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metadata_utils.h"
#include "gpu/egl/egl_fence.h"
#include "gpu/gles/texture_2d.h"

namespace cros {

namespace {
const int kSyncWaitTimeoutMs = 8;
const base::TimeDelta kMaximumMetricsSessionDuration = base::Seconds(3600);

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

void LogAverageLatency(base::TimeDelta latency) {
  static base::NoDestructor<std::deque<float>> latencies;
  auto const count = static_cast<float>(latencies->size());
  if (count > 60) {
    auto avg = std::reduce(latencies->begin(), latencies->end()) / count;
    VLOGF(1) << "Avg frame latency: " << avg;
    latencies->clear();
  }
  latencies->push_back(latency.InMillisecondsF());
}

constexpr char kEffectKey[] = "effect";
constexpr char kBlurLevelKey[] = "blur_level";
constexpr char kGpuApiKey[] = "gpu_api";
constexpr char kRelightingGpuApiKey[] = "relighting_gpu_api";
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
  DETACH_FROM_SEQUENCE(sequence_checker_);
  DETACH_FROM_THREAD(gl_thread_checker_);

  if (!config_.IsValid()) {
    LOGF(ERROR) << "Cannot load valid config. Turning off feature by default";
  }
  CHECK(gl_thread_.Start());

  // TODO(b/260656766): find a better task runner than the one from gl_thread
  // for metrics_uploader_. It would be nice to use
  // base::ThreadPool::CreateSequencedTaskRunner, but seems that
  // ThreadPoolInstance::Set() hasn't been set up in the camera stack, and it's
  // not the responsibility of this class to do that.
  metrics_uploader_ =
      std::make_unique<EffectsMetricsUploader>(gl_thread_.task_runner());

  bool ret;
  gl_thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&EffectsStreamManipulator::SetupGlThread,
                     base::Unretained(this)),
      &ret);
  if (!ret) {
    LOGF(ERROR) << "Failed to start GL thread. Turning off feature by default";
  }

  if (!runtime_options_->GetDlcRootPath().empty()) {
    CreatePipeline(base::FilePath(runtime_options_->GetDlcRootPath()));
  }
}

EffectsStreamManipulator::~EffectsStreamManipulator() {
  // UploadAndResetMetricsData currently posts a task to the gl_thread task
  // runner (see constructor above). If we change that, we need to ensure the
  // upload task is complete before the destructor exits, or change the
  // behaviour to be synchronous in this situation.
  UploadAndResetMetricsData();
  gl_thread_.Stop();
  ResetState();
}

bool EffectsStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  callbacks_ = std::move(callbacks);
  return true;
}

bool EffectsStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  UploadAndResetMetricsData();
  ResetState();

  base::AutoLock lock(stream_contexts_lock_);
  base::span<camera3_stream_t* const> client_requested_streams =
      stream_config->GetStreams();
  std::vector<camera3_stream_t*> modified_streams;
  for (auto* s : client_requested_streams) {
    if (s->stream_type != CAMERA3_STREAM_OUTPUT) {
      // Only output buffers are supported.
      modified_streams.push_back(s);
      continue;
    }

    if (s->format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
        s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED ||
        s->format == HAL_PIXEL_FORMAT_BLOB) {
      if (s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED &&
          (s->usage & GRALLOC_USAGE_HW_CAMERA_ZSL) ==
              GRALLOC_USAGE_HW_CAMERA_ZSL) {
        // Ignore ZSL streams.
        modified_streams.push_back(s);
        continue;
      }

      if (s->format == HAL_PIXEL_FORMAT_BLOB) {
        // TODO(skyostil): Support BLOB streams.
        modified_streams.push_back(s);
        continue;
      }

      auto context = std::make_unique<StreamContext>();
      context->original_stream = s;
      context->effect_stream = std::make_unique<camera3_stream_t>(*s);
      context->effect_stream->format = HAL_PIXEL_FORMAT_YCbCr_420_888;
      stream_contexts_.emplace_back(std::move(context));
      modified_streams.push_back(s);
    }
  }
  stream_config->SetStreams(modified_streams);
  return true;
}

void EffectsStreamManipulator::ResetState() {
  base::AutoLock lock(stream_contexts_lock_);
  stream_contexts_.clear();
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
      desc->GetMetadata<int64_t>(ANDROID_SENSOR_TIMESTAMP);
  return timestamp.size() == 1 ? std::make_optional(timestamp[0] / 1000)
                               : std::nullopt;
}

bool EffectsStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto processing_time_start = base::TimeTicks::Now();
  if (!process_thread_) {
    process_thread_ = base::ThreadTaskRunnerHandle::Get();
    config_.SetCallback(base::BindRepeating(
        &EffectsStreamManipulator::OnOptionsUpdated, base::Unretained(this)));
  }

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
    SetEffect(new_config);
  }

  if (!last_set_effect_config_.HasEnabledEffects())
    return true;

  auto timestamp = TryGetSensorTimestamp(&result);
  if (!timestamp.has_value()) {
    timestamp = last_timestamp_;
  }

  base::AutoLock lock(stream_contexts_lock_);
  for (auto& result_buffer : result.AcquireOutputBuffers()) {
    StreamContext* stream_context = nullptr;
    for (auto& s : stream_contexts_) {
      if (s->original_stream == result_buffer.stream()) {
        stream_context = s.get();
        break;
      }
    }
    if (!stream_context) {
      // Not a stream we care about, so just pass it through.
      result.AppendOutputBuffer(std::move(result_buffer));
      continue;
    }

    if (result_buffer.status() != CAMERA3_BUFFER_STATUS_OK) {
      LOGF(ERROR) << "EffectsStreamManipulator received failed buffer: "
                  << result.frame_number();
      return false;
    }

    if (!RenderEffect(result_buffer, *timestamp)) {
      continue;
    }
    result.AppendOutputBuffer(std::move(result_buffer));
  }

  auto frame_processed_time = base::TimeTicks::Now();
  // If we've recorded at least one frame
  if (last_processed_frame_timestamp_ != base::TimeTicks()) {
    metrics_.RecordFrameProcessingInterval(
        last_set_effect_config_,
        frame_processed_time - last_processed_frame_timestamp_);
  }
  last_processed_frame_timestamp_ = frame_processed_time;
  metrics_.RecordFrameProcessingLatency(
      last_set_effect_config_, frame_processed_time - processing_time_start);
  if (metrics_uploader_->TimeSinceLastUpload() >
      kMaximumMetricsSessionDuration) {
    UploadAndResetMetricsData();
  }

  if (VLOG_IS_ON(1)) {
    LogAverageLatency(base::TimeTicks::Now() - processing_time_start);
  }
  return true;
}

bool EffectsStreamManipulator::RenderEffect(Camera3StreamBuffer& result_buffer,
                                            int64_t timestamp) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(result_buffer.status() == CAMERA3_BUFFER_STATUS_OK);
  if (!result_buffer.WaitOnAndClearReleaseFence(kSyncWaitTimeoutMs)) {
    LOGF(ERROR) << "Timed out waiting for input buffer";
    return false;
  }

  buffer_handle_t buffer_handle = *result_buffer.buffer();

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

  // Mediapipe requires timestamps to be strictly increasing for a given
  // pipeline. If we receive non-monotonic timestamps or render the pipeline for
  // multiple streams in parallel, make sure the same timestamp isn't repeated.
  timestamp = std::max(timestamp, last_timestamp_ + 1);
  last_timestamp_ = timestamp;

  frame_status_ = absl::OkStatus();
  pipeline_->ProcessFrame(timestamp, input_image_rgba_.texture().handle(),
                          input_image_rgba_.texture().width(),
                          input_image_rgba_.texture().height());
  pipeline_->Wait();
  if (!frame_status_.ok()) {
    LOG(ERROR) << frame_status_.message();
    return false;
  }
  result_buffer.mutable_raw_buffer().status = CAMERA3_BUFFER_STATUS_OK;
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
  std::string effect;
  if (GetStringFromKey(json_values, kEffectKey, &effect)) {
    if (effect == std::string("blur")) {
      new_config.blur_enabled = true;
    } else if (effect == std::string("replace")) {
      new_config.replace_enabled = true;
    } else if (effect == std::string("relight")) {
      new_config.relight_enabled = true;
    } else if (effect == std::string("blur_relight")) {
      new_config.blur_enabled = true;
      new_config.relight_enabled = true;
    } else if (effect == std::string("none")) {
      new_config.blur_enabled = false;
      new_config.relight_enabled = false;
      new_config.replace_enabled = false;
    } else {
      LOGF(WARNING) << "Unknown Effect: " << effect;
      return;
    }
    LOGF(INFO) << "Effect Updated: " << effect;
  }
  LoadIfExist(json_values, kBlurEnabled, &new_config.blur_enabled);
  LoadIfExist(json_values, kReplaceEnabled, &new_config.replace_enabled);
  LoadIfExist(json_values, kRelightEnabled, &new_config.relight_enabled);

  std::string blur_level;
  if (GetStringFromKey(json_values, kBlurLevelKey, &blur_level)) {
    if (blur_level == "lowest") {
      new_config.blur_level = BlurLevel::kLowest;
    } else if (blur_level == "light") {
      new_config.blur_level = BlurLevel::kLight;
    } else if (blur_level == "medium") {
      new_config.blur_level = BlurLevel::kMedium;
    } else if (blur_level == "heavy") {
      new_config.blur_level = BlurLevel::kHeavy;
    } else if (blur_level == "maximum") {
      new_config.blur_level = BlurLevel::kMaximum;
    } else {
      LOGF(WARNING) << "Unknown Blur Level: " << blur_level;
      return;
    }
    LOGF(INFO) << "Blur Level: " << blur_level;
  }

  std::string gpu_api;
  if (GetStringFromKey(json_values, kGpuApiKey, &gpu_api)) {
    if (gpu_api == "opengl") {
      new_config.segmentation_gpu_api = GpuApi::kOpenGL;
      new_config.relighting_gpu_api = GpuApi::kOpenGL;
    } else if (gpu_api == "opencl") {
      new_config.segmentation_gpu_api = GpuApi::kOpenCL;
      new_config.relighting_gpu_api = GpuApi::kOpenCL;
    } else if (gpu_api == "any") {
      new_config.segmentation_gpu_api = GpuApi::kAny;
      new_config.relighting_gpu_api = GpuApi::kAny;
    } else {
      LOGF(WARNING) << "Unknown GPU API: " << gpu_api;
      return;
    }
    LOGF(INFO) << "GPU API: " << gpu_api;
  }

  std::string relighting_gpu_api;
  if (GetStringFromKey(json_values, kRelightingGpuApiKey,
                       &relighting_gpu_api)) {
    if (relighting_gpu_api == "opengl") {
      new_config.relighting_gpu_api = GpuApi::kOpenGL;
    } else if (relighting_gpu_api == "opencl") {
      new_config.relighting_gpu_api = GpuApi::kOpenCL;
    } else if (relighting_gpu_api == "any") {
      new_config.relighting_gpu_api = GpuApi::kAny;
    } else {
      LOGF(WARNING) << "Unknown Relighting GPU API: " << gpu_api;
      return;
    }
    LOGF(INFO) << "Relighting GPU API: " << relighting_gpu_api;
  }

  process_thread_->PostTask(
      FROM_HERE, base::BindOnce(&EffectsStreamManipulator::SetEffect,
                                base::Unretained(this), std::move(new_config)));
}

void EffectsStreamManipulator::SetEffect(EffectsConfig new_config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (pipeline_) {
    pipeline_->SetEffect(&new_config, set_effect_callback_);
    last_set_effect_config_ = new_config;

    if (new_config.HasEnabledEffects()) {
      metrics_.RecordSelectedEffect(new_config);
    } else {
      // If no effect is set, stop recording frame intervals
      last_processed_frame_timestamp_ = base::TimeTicks();
    }
  } else {
    LOGF(WARNING) << "SetEffect called, but pipeline not ready.";
  }
}

bool EffectsStreamManipulator::SetupGlThread() {
  DCHECK_CALLED_ON_VALID_THREAD(gl_thread_checker_);
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
  DCHECK_CALLED_ON_VALID_THREAD(gl_thread_checker_);

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
  DCHECK_CALLED_ON_VALID_THREAD(gl_thread_checker_);

  bool conv_result = image_processor_->NV12ToRGBA(input_image_yuv_.y_texture(),
                                                  input_image_yuv_.uv_texture(),
                                                  input_image_rgba_.texture());
  glFinish();
  return conv_result;
}

void EffectsStreamManipulator::RGBAToNV12(GLuint texture,
                                          uint32_t width,
                                          uint32_t height) {
  DCHECK_CALLED_ON_VALID_THREAD(gl_thread_checker_);

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
}

void EffectsStreamManipulator::UploadAndResetMetricsData() {
  EffectsMetricsData metrics_copy(metrics_);
  metrics_ = EffectsMetricsData();
  metrics_uploader_->UploadMetricsData(std::move(metrics_copy));
}

}  // namespace cros
