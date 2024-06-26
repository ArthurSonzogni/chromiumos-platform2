/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/effects/effects_stream_manipulator.h"

#include <GLES3/gl3.h>

#include <cutils/native_handle.h>
#include <hardware/camera3.h>
#include <sync/sync.h>
#include <system/camera_metadata.h>
#include <unistd.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/flat_set.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback_forward.h>
#include <base/functional/callback_helpers.h>
#include <base/location.h>
#include <base/no_destructor.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/synchronization/lock.h>
#include <base/thread_annotations.h>
#include <base/threading/thread_checker.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <base/values.h>
#include <brillo/files/file_util.h>
#include <ml_core/dlc/dlc_ids.h>
#include <ml_core/effects_pipeline.h>
#include <ml_core/effects_pipeline_types.h>
#include <ml_core/opencl_caching/constants.h>
#include <ml_core/opencl_caching/utils.h>

#include "common/camera_buffer_pool.h"
#include "common/camera_hal3_helpers.h"
#include "common/reloadable_config_file.h"
#include "common/still_capture_processor.h"
#include "common/stream_manipulator.h"
#include "common/stream_manipulator_helper.h"
#include "cros-camera/camera_metrics.h"
#include "features/effects/effects_metrics.h"
#include "features/effects/tracing.h"
#include "gpu/gles/texture_2d.h"
#include "gpu/image_processor.h"
#include "gpu/shared_image.h"

namespace cros {

namespace {

const int kSyncWaitTimeoutMs = 1000;
const base::TimeDelta kMaximumMetricsSessionDuration = base::Seconds(3600);
// Practically most HALs configure <= 6 in-flight requests. Too high value
// may cause OOM; too low can cause frame drops in the graph.
const int kGraphMaxFramesInflightDefault = 7;

// "effect" key's value needs to be "none" or an combination of "blur",
// "replace", "relight", "retouch" separated by underscore "_".
// E.g. "blur_relight".
constexpr char kEffectKey[] = "effect";
constexpr char kBlurLevelKey[] = "blur_level";
constexpr char kRetouchStrength[] = "retouch_strength";
constexpr char kDelegateKey[] = "delegate";
constexpr char kRelightingDelegateKey[] = "relighting_delegate";
constexpr char kGpuApiKey[] = "gpu_api";
constexpr char kRelightingGpuApiKey[] = "relighting_gpu_api";
constexpr char kStableDelegateSettingsFileKey[] =
    "stable_delegate_settings_file";
constexpr char kBlurEnabled[] = "blur_enabled";
constexpr char kReplaceEnabled[] = "replace_enabled";
constexpr char kRelightEnabled[] = "relight_enabled";
constexpr char kRetouchEnabled[] = "retouch_enabled";
constexpr char kSegmentationModelTypeKey[] = "segmentation_model_type";
constexpr char kDefaultSegmentationModelTypeKey[] =
    "default_segmentation_model_type";

constexpr uint32_t kRGBAFormat = HAL_PIXEL_FORMAT_RGBX_8888;
constexpr uint32_t kRGBABufferUsage =
    GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_SW_READ_NEVER;

const base::FilePath kEffectsRunningMarker("/run/camera/effects_running");
const base::TimeDelta kEffectsRunningMarkerLifetime = base::Seconds(10);
// TODO(b:242631540) Find permanent location for this file
const base::FilePath kOverrideEffectsConfigFile(
    "/run/camera/effects/effects_config_override.json");
const base::FilePath kEnableRetouchWithRelight(
    "/run/camera/enable_retouch_with_relight");
const base::FilePath kEnableOnlyRetouch("/run/camera/enable_only_retouch");
constexpr char kTfliteStableDelegateSettingsFile[] =
    "/etc/tflite/settings.json";

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

bool GetDoubleFromKey(const base::Value::Dict& obj,
                      const std::string& key,
                      double* value) {
  std::optional<double> val = obj.FindDouble(key);
  if (!val.has_value()) {
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

void DeleteEffectsMarkerFile() {
  if (!base::PathExists(kEffectsRunningMarker))
    return;

  if (!brillo::DeleteFile(kEffectsRunningMarker)) {
    LOGF(WARNING) << "Couldn't delete effects marker file";
  }
}

// Creates a file that indicates an attempt to start
// the effects pipeline has been made. If this causes the
// camera stack to crash, the file will be left there
// and the opencl-cacher-failsafe upstart job will
// clear the cache. Returns a timer object that will delete
// the marker file after the duration defined in kEffectsRunningMarkerLifetime
std::unique_ptr<base::OneShotTimer> CreateEffectsMarkerFile() {
  if (!base::WriteFile(kEffectsRunningMarker, "")) {
    LOGF(WARNING) << "Couldn't create effects marker file";
    return nullptr;
  }
  auto timer = std::make_unique<base::OneShotTimer>();
  timer->Start(FROM_HERE, kEffectsRunningMarkerLifetime,
               base::BindOnce(&DeleteEffectsMarkerFile));
  return timer;
}

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

Delegate GetDelegateFromInferenceBackend(
    cros::mojom::InferenceBackend backend) {
  switch (backend) {
    case cros::mojom::InferenceBackend::kGpu:
      return Delegate::kGpu;
    case cros::mojom::InferenceBackend::kNpu:
      return Delegate::kStable;
    default:
      LOGF(WARNING) << "Got unexpected inference backend " << backend;
      return Delegate::kGpu;
  }
}

EffectsConfig ConvertMojoConfig(
    cros::mojom::EffectsConfigPtr effects_config,
    SegmentationModelType default_segmentation_model_type) {
  // Note: We don't copy over the GPU api fields here, since we have no
  //       need to control them from Chrome at this stage. It will use
  //       the default from effects_pipeline_types.h
  auto config = EffectsConfig{
      .relight_enabled = effects_config->relight_enabled,
      .blur_enabled = effects_config->blur_enabled,
      .replace_enabled = effects_config->replace_enabled,
      .blur_level = static_cast<cros::BlurLevel>(effects_config->blur_level),
      .segmentation_delegate = GetDelegateFromInferenceBackend(
          effects_config->segmentation_inference_backend),
      .relighting_delegate = GetDelegateFromInferenceBackend(
          effects_config->relighting_inference_backend),
      .graph_max_frames_in_flight = effects_config->graph_max_frames_in_flight,
      .wait_on_render = true,
      .segmentation_model_type = static_cast<cros::SegmentationModelType>(
          effects_config->segmentation_model),
  };
  if (config.segmentation_delegate == Delegate::kStable ||
      config.relighting_delegate == Delegate::kStable) {
    if (base::PathIsReadable(
            base::FilePath(kTfliteStableDelegateSettingsFile))) {
      static_assert(sizeof(kTfliteStableDelegateSettingsFile) <=
                    sizeof(config.stable_delegate_settings_file));
      base::strlcpy(config.stable_delegate_settings_file,
                    kTfliteStableDelegateSettingsFile,
                    sizeof(config.stable_delegate_settings_file));
    } else {
      LOGF(WARNING) << kTfliteStableDelegateSettingsFile
                    << " is not readable, use GPU delegate instead";
      config.segmentation_delegate = Delegate::kGpu;
      config.relighting_delegate = Delegate::kGpu;
    }
  }

  // Resolve segmentation model from Auto or HD (default) to the system default.
  // TODO(b/297450516): Fix mojo segmentation to be 'auto' by default.
  // This is to avoid resetting the pipeline when the model changes from Auto to
  // HD.
  if (effects_config->segmentation_model == mojom::SegmentationModel::kAuto ||
      effects_config->segmentation_model ==
          mojom::SegmentationModel::kHighResolution) {
    config.segmentation_model_type = default_segmentation_model_type;
  }
  if (effects_config->background_filepath) {
    base::FilePath path =
        base::FilePath("/run/camera/")
            .Append(effects_config->background_filepath->path);
    base::strlcpy(config.background_image_asset, path.value().c_str(),
                  sizeof(config.background_image_asset));
  }
  if (effects_config->light_intensity) {
    config.light_intensity = *effects_config->light_intensity;
  }
  if (base::PathExists(kEnableOnlyRetouch)) {
    config.face_retouch_enabled = config.relight_enabled;
    config.relight_enabled = false;
  } else if (base::PathExists(kEnableRetouchWithRelight)) {
    config.face_retouch_enabled = config.relight_enabled;
  }
  return config;
}

bool ParseSegmentationModelType(const std::string& model,
                                SegmentationModelType& output) {
  if (model == "auto") {
    output = SegmentationModelType::kAuto;
  } else if (model == "hd") {
    output = SegmentationModelType::kHd;
  } else if (model == "effnet384") {
    output = SegmentationModelType::kEffnet384;
  } else if (model == "full") {
    output = SegmentationModelType::kFull;
  } else {
    LOGF(WARNING) << "Unknown Segmentation Model Type: " << model;
    return false;
  }
  return true;
}

class EffectsPipelineTracker {
 public:
  explicit EffectsPipelineTracker(EffectsMetricsData& metrics)
      : metrics_(metrics) {}

  void Reset() {
    first_frame_received_ = false;
    dropped_frame_count_ = 0;
  }

  void TrackDroppedFrame() {
    dropped_frame_count_++;
    if (first_frame_received_) {
      LOGF(ERROR) << "Failed to process effects pipeline";
      metrics_.RecordError(CameraEffectError::kPipelineFailed);
    } else {
      VLOGF(1) << "Failed to process effects pipeline at startup";
    }
  }

  void TrackProcessedFrame() {
    if (!first_frame_received_) {
      first_frame_received_ = true;
      LOGF(INFO) << "Dropped frames count at effects pipeline startup: "
                 << dropped_frame_count_;
    }
  }

 private:
  size_t dropped_frame_count_ = 0;
  std::atomic<bool> first_frame_received_ = false;
  EffectsMetricsData metrics_;
};

}  // namespace

class EffectsStreamManipulatorImpl : public EffectsStreamManipulator {
 public:
  // callback used to signal that an effect has taken effect.
  // Once the callback is fired it is guaranteed that all subsequent
  // frames will have the effect applied.
  // TODO(b:263440749): update callback type
  EffectsStreamManipulatorImpl(
      base::FilePath config_file_path,
      RuntimeOptions* runtime_options,
      std::unique_ptr<StillCaptureProcessor> still_capture_processor,
      std::string camera_module_name,
      void (*callback)(bool) = nullptr);
  EffectsStreamManipulatorImpl(const EffectsStreamManipulatorImpl&) = delete;
  EffectsStreamManipulatorImpl& operator=(const EffectsStreamManipulatorImpl&) =
      delete;
  ~EffectsStreamManipulatorImpl() override;

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
  scoped_refptr<base::SingleThreadTaskRunner> GetTaskRunner() override;

  void OnFrameProcessed(int64_t timestamp,
                        GLuint texture,
                        uint32_t width,
                        uint32_t height);

 private:
  // States for async effects pipeline processing. On destroy, the output
  // buffers are returned to the client.
  struct ProcessContext {
    SharedImage yuv_image;
    std::optional<CameraBufferPool::Buffer> rgba_buffer;
    SharedImage rgba_image;
    base::TimeTicks start_time;
  };

  // State related to a single frame capture.
  struct CaptureContext : public StreamManipulatorHelper::PrivateContext {
    EffectsConfig effects;
    ProcessContext video_process_context;
    ProcessContext still_process_context;
  };

  void OnOptionsUpdated(const base::Value::Dict& json_values);

  void SetEffect(EffectsConfig new_config);
  bool SetupGlThread(base::FilePath config_file_path);
  // Load the pipeline with effects config from |runtime_options_|.
  bool EnsurePipelineSetupOnGlThread();
  void ShutdownOnGlThread();
  void CreatePipeline(const base::FilePath& dlc_root_path);
  void UploadAndResetMetricsData();
  void ResetState();
  void PostProcess(int64_t timestamp,
                   GLuint texture,
                   uint32_t width,
                   uint32_t height);
  void OnProcessTask(ScopedProcessTask task);

  std::atomic<bool> needs_reset_ = false;

  std::unique_ptr<ReloadableConfigFile> config_;
  base::FilePath config_file_path_;
  bool override_config_exists_ GUARDED_BY_CONTEXT(gl_thread_checker_) = false;
  RuntimeOptions* runtime_options_;
  SegmentationModelType default_segmentation_model_type_
      GUARDED_BY_CONTEXT(gl_thread_checker_) = SegmentationModelType::kHd;

  // Maximum number of frames that can be queued into effects pipeline.
  // Use the default value to setup the pipeline early.
  // This will be updated at stream configuration.
  uint32_t graph_max_frames_in_flight_ = kGraphMaxFramesInflightDefault;

  EffectsConfig active_runtime_effects_config_
      GUARDED_BY_CONTEXT(gl_thread_checker_) = EffectsConfig();
  // Config state. last_set_effect_ can be different to
  // active_runtime_effects_config_ when the effect is set
  // via the ReloadableConfig mechanism.
  EffectsConfig last_set_effect_config_ GUARDED_BY_CONTEXT(gl_thread_checker_) =
      EffectsConfig();

  std::unique_ptr<EffectsPipeline> pipeline_
      GUARDED_BY_CONTEXT(gl_thread_checker_);

  std::unique_ptr<EglContext> egl_context_
      GUARDED_BY_CONTEXT(gl_thread_checker_);
  std::unique_ptr<GpuImageProcessor> image_processor_
      GUARDED_BY_CONTEXT(gl_thread_checker_);

  std::unique_ptr<CameraBufferPool> video_rgba_buffer_pool_
      GUARDED_BY_CONTEXT(gl_thread_checker_);
  std::unique_ptr<CameraBufferPool> still_rgba_buffer_pool_
      GUARDED_BY_CONTEXT(gl_thread_checker_);
  std::optional<base::TimeTicks> video_process_last_start_time_
      GUARDED_BY_CONTEXT(gl_thread_checker_);
  std::optional<base::TimeTicks> still_process_last_start_time_
      GUARDED_BY_CONTEXT(gl_thread_checker_);
  int64_t last_timestamp_ GUARDED_BY_CONTEXT(gl_thread_checker_) = 0;
  std::unique_ptr<StillCaptureProcessor> still_capture_processor_
      GUARDED_BY_CONTEXT(gl_thread_checker_);
  std::string camera_module_name_;
  std::unique_ptr<StreamManipulatorHelper> helper_;
  base::flat_map<int64_t /*timestamp*/, ScopedProcessTask> tasks_
      GUARDED_BY_CONTEXT(gl_thread_checker_);

  CameraThread gl_thread_;

  void (*set_effect_callback_)(bool);

  THREAD_CHECKER(gl_thread_checker_);

  EffectsMetricsData metrics_;
  std::unique_ptr<EffectsMetricsUploader> metrics_uploader_;
  EffectsPipelineTracker effects_pipeline_tracker_;

  std::unique_ptr<base::OneShotTimer> marker_file_timer_;
};

std::unique_ptr<EffectsStreamManipulator> EffectsStreamManipulator::Create(
    base::FilePath config_file_path,
    RuntimeOptions* runtime_options,
    std::unique_ptr<StillCaptureProcessor> still_capture_processor,
    std::string camera_module_name,
    void (*callback)(bool)) {
  return std::make_unique<EffectsStreamManipulatorImpl>(
      config_file_path, runtime_options, std::move(still_capture_processor),
      std::move(camera_module_name), callback);
}

EffectsStreamManipulatorImpl::EffectsStreamManipulatorImpl(
    base::FilePath config_file_path,
    RuntimeOptions* runtime_options,
    std::unique_ptr<StillCaptureProcessor> still_capture_processor,
    std::string camera_module_name,
    void (*callback)(bool))
    : config_file_path_(config_file_path),
      runtime_options_(runtime_options),
      still_capture_processor_(std::move(still_capture_processor)),
      camera_module_name_(std::move(camera_module_name)),
      gl_thread_("EffectsGlThread"),
      set_effect_callback_(callback),
      effects_pipeline_tracker_(metrics_) {
  DETACH_FROM_THREAD(gl_thread_checker_);

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
      base::BindOnce(&EffectsStreamManipulatorImpl::SetupGlThread,
                     base::Unretained(this), std::move(config_file_path)),
      &ret);
  if (!ret) {
    LOGF(ERROR) << "Failed to start GL thread. Turning off feature by default";
    metrics_.RecordError(CameraEffectError::kGPUInitializationError);
  } else {
    gl_thread_.PostTaskAsync(
        FROM_HERE,
        base::BindOnce(
            &EffectsStreamManipulatorImpl::EnsurePipelineSetupOnGlThread,
            base::Unretained(this)));
  }
}

EffectsStreamManipulatorImpl::~EffectsStreamManipulatorImpl() {
  DeleteEffectsMarkerFile();
  // UploadAndResetMetricsData currently posts a task to the gl_thread task
  // runner (see constructor above). If we change that, we need to ensure the
  // upload task is complete before the destructor exits, or change the
  // behaviour to be synchronous in this situation.
  UploadAndResetMetricsData();
  gl_thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&EffectsStreamManipulatorImpl::ShutdownOnGlThread,
                     base::Unretained(this)));
  gl_thread_.Stop();
}

void EffectsStreamManipulatorImpl::ShutdownOnGlThread() {
  DCHECK_CALLED_ON_VALID_THREAD(gl_thread_checker_);
  TRACE_EFFECTS();
  config_.reset();
  marker_file_timer_.reset();
  if (pipeline_) {
    pipeline_.reset();
  }
  ResetState();
  helper_.reset();
}

bool EffectsStreamManipulatorImpl::Initialize(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  helper_ = std::make_unique<StreamManipulatorHelper>(
      StreamManipulatorHelper::Config{
          .process_mode = ProcessMode::kVideoAndStillProcess,
          .result_metadata_tags_to_update = {ANDROID_SENSOR_TIMESTAMP},
      },
      camera_module_name_, static_info, std::move(callbacks),
      base::BindRepeating(&EffectsStreamManipulatorImpl::OnProcessTask,
                          base::Unretained(this)),
      GetCropScaleImageCallback(gl_thread_.task_runner(),
                                image_processor_.get()),
      std::move(still_capture_processor_), gl_thread_.task_runner());
  return true;
}

bool EffectsStreamManipulatorImpl::EnsurePipelineSetupOnGlThread() {
  DCHECK_CALLED_ON_VALID_THREAD(gl_thread_checker_);
  if (!pipeline_ &&
      !runtime_options_->GetDlcRootPath(dlc_client::kMlCoreDlcId).empty()) {
    CreatePipeline(base::FilePath(
        runtime_options_->GetDlcRootPath(dlc_client::kMlCoreDlcId)));
  }
  if (!pipeline_)
    return false;

  auto new_config = ConvertMojoConfig(runtime_options_->GetEffectsConfig(),
                                      default_segmentation_model_type_);
  if (active_runtime_effects_config_ != new_config) {
    active_runtime_effects_config_ = new_config;
    // Ignore the mojo config if the override config file is being used. This is
    // to avoid race conditions in tests where Chrome is also setting a default
    // (no-op) config mojo. Note that this flag isn't unset, so the camera
    // service must be restarted after the override config file has been
    // deleted.
    if (!override_config_exists_) {
      SetEffect(new_config);
    } else {
      LOGF(WARNING) << "Override config exists, ignoring mojo effect settings: "
                    << kOverrideEffectsConfigFile;
    }
  }
  return true;
}

bool EffectsStreamManipulatorImpl::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  TRACE_EFFECTS([&](perfetto::EventContext ctx) {
    stream_config->PopulateEventAnnotation(ctx);
  });
  UploadAndResetMetricsData();

  // |gl_thread_| might be busy loading the pipeline.
  // Blocking here directly adds to overall ConfigureStreams latency.
  if (needs_reset_) {
    gl_thread_.PostTaskSync(
        FROM_HERE, base::BindOnce(&EffectsStreamManipulatorImpl::ResetState,
                                  base::Unretained(this)));
  }
  needs_reset_ = true;

  if (!helper_->PreConfigure(stream_config)) {
    return false;
  }
  if (const camera3_stream_t* s = helper_->video_process_input_stream()) {
    metrics_.RecordStreamSize(CameraEffectStreamType::kYuv,
                              s->width * s->height);
  }
  if (const camera3_stream_t* s = helper_->still_process_input_stream()) {
    metrics_.RecordStreamSize(CameraEffectStreamType::kBlob,
                              s->width * s->height);
  }
  TRACE_EVENT_INSTANT(kCameraTraceCategoryEffects, "ModifiedStreamConfig",
                      [&](perfetto::EventContext ctx) {
                        stream_config->PopulateEventAnnotation(ctx);
                      });
  return true;
}

void EffectsStreamManipulatorImpl::ResetState() {
  DCHECK_CALLED_ON_VALID_THREAD(gl_thread_checker_);
  effects_pipeline_tracker_.Reset();
  tasks_.clear();
  video_rgba_buffer_pool_.reset();
  still_rgba_buffer_pool_.reset();
  video_process_last_start_time_.reset();
  still_process_last_start_time_.reset();
  last_timestamp_ = 0;
  needs_reset_ = false;
}

bool EffectsStreamManipulatorImpl::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  TRACE_EFFECTS([&](perfetto::EventContext ctx) {
    stream_config->PopulateEventAnnotation(ctx);
  });

  helper_->PostConfigure(stream_config);

  uint32_t total_max_buffers = 0;
  if (const camera3_stream_t* s = helper_->video_process_input_stream()) {
    total_max_buffers += s->max_buffers;
    video_rgba_buffer_pool_ =
        std::make_unique<CameraBufferPool>(CameraBufferPool::Options{
            .width = s->width,
            .height = s->height,
            .format = kRGBAFormat,
            .usage = kRGBABufferUsage,
            .max_num_buffers = s->max_buffers + 1,
        });
  }
  if (const camera3_stream_t* s = helper_->still_process_input_stream()) {
    total_max_buffers += 1;
    still_rgba_buffer_pool_ =
        std::make_unique<CameraBufferPool>(CameraBufferPool::Options{
            .width = s->width,
            .height = s->height,
            .format = kRGBAFormat,
            .usage = kRGBABufferUsage,
            .max_num_buffers = 2,
        });
  }
  graph_max_frames_in_flight_ =
      std::max(graph_max_frames_in_flight_, total_max_buffers);

  TRACE_EVENT_INSTANT(kCameraTraceCategoryEffects, "ModifiedStreamConfig",
                      [&](perfetto::EventContext ctx) {
                        stream_config->PopulateEventAnnotation(ctx);
                      });
  return true;
}

bool EffectsStreamManipulatorImpl::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  return true;
}

bool EffectsStreamManipulatorImpl::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  if (!gl_thread_.task_runner()->BelongsToCurrentThread()) {
    bool ret = false;
    gl_thread_.PostTaskSync(
        FROM_HERE,
        base::BindOnce(&EffectsStreamManipulatorImpl::ProcessCaptureRequest,
                       base::Unretained(this), base::Unretained(request)),
        &ret);
    return ret;
  }
  TRACE_EFFECTS("frame_number", request->frame_number());

  auto ctx = std::make_unique<CaptureContext>();
  ctx->effects = last_set_effect_config_;
  const bool bypass_process = runtime_options_->sw_privacy_switch_state() ==
                                  mojom::CameraPrivacySwitchState::ON ||
                              !EnsurePipelineSetupOnGlThread() ||
                              !ctx->effects.HasEnabledEffects();
  helper_->HandleRequest(request, bypass_process, std::move(ctx));

  base::span<const int32_t> fps_range =
      request->GetMetadata<int32_t>(ANDROID_CONTROL_AE_TARGET_FPS_RANGE);
  if (!fps_range.empty()) {
    metrics_.RecordRequestedFrameRate(fps_range[1]);
  }
  return true;
}

bool EffectsStreamManipulatorImpl::ProcessCaptureResult(
    Camera3CaptureDescriptor result) {
  DCHECK_CALLED_ON_VALID_THREAD(gl_thread_checker_);
  TRACE_EFFECTS("frame_number", result.frame_number());

  helper_->HandleResult(std::move(result));
  return true;
}

void EffectsStreamManipulatorImpl::OnProcessTask(ScopedProcessTask task) {
  DCHECK_CALLED_ON_VALID_THREAD(gl_thread_checker_);

  auto& capture_ctx = *task->GetPrivateContextAs<CaptureContext>();
  auto& process_ctx = task->IsStillCapture()
                          ? capture_ctx.still_process_context
                          : capture_ctx.video_process_context;

  process_ctx.start_time = base::TimeTicks::Now();
  if (task->IsStillCapture()) {
    if (still_process_last_start_time_.has_value()) {
      metrics_.RecordFrameProcessingInterval(
          capture_ctx.effects, CameraEffectStreamType::kBlob,
          process_ctx.start_time - still_process_last_start_time_.value());
    }
    still_process_last_start_time_.emplace(process_ctx.start_time);
  } else {
    if (video_process_last_start_time_.has_value()) {
      metrics_.RecordFrameProcessingInterval(
          capture_ctx.effects, CameraEffectStreamType::kYuv,
          process_ctx.start_time - video_process_last_start_time_.value());
    }
    video_process_last_start_time_.emplace(process_ctx.start_time);
  }
  if (metrics_uploader_->TimeSinceLastUpload() >
      kMaximumMetricsSessionDuration) {
    UploadAndResetMetricsData();
  }

  int64_t timestamp = [&]() {
    const camera_metadata_entry_t entry =
        task->result_metadata().find(ANDROID_SENSOR_TIMESTAMP);
    CHECK_GT(entry.count, 0);
    return entry.data.i64[0] / 1000;
  }();
  TRACE_EFFECTS(perfetto::Flow::ProcessScoped(
                    reinterpret_cast<uintptr_t>(task->input_buffer())),
                "frame_number", task->frame_number(), "timestamp", timestamp);

  // Mediapipe requires timestamps to be strictly increasing for a given
  // pipeline. If we receive non-monotonic timestamps or render the pipeline
  // for multiple streams in parallel, make sure the same timestamp isn't
  // repeated.
  timestamp = std::max(timestamp, last_timestamp_ + 1);
  last_timestamp_ = timestamp;

  base::ScopedFD input_release_fence = task->TakeInputReleaseFence();
  if (input_release_fence.is_valid() &&
      sync_wait(input_release_fence.get(), kSyncWaitTimeoutMs) != 0) {
    LOGF(ERROR) << "Sync wait timed out on input frame "
                << task->frame_number();
    task->Fail();
    metrics_.RecordError(CameraEffectError::kSyncWaitTimeout);
    return;
  }
  base::ScopedFD output_acquire_fence = task->TakeOutputAcquireFence();
  if (output_acquire_fence.is_valid() &&
      sync_wait(output_acquire_fence.get(), kSyncWaitTimeoutMs) != 0) {
    LOGF(ERROR) << "Sync wait timed out on output frame "
                << task->frame_number();
    task->Fail();
    metrics_.RecordError(CameraEffectError::kSyncWaitTimeout);
    return;
  }

  process_ctx.yuv_image = SharedImage::CreateFromBuffer(
      task->input_buffer(), Texture2D::Target::kTarget2D,
      /*separate_yuv_textures=*/true);
  if (!process_ctx.yuv_image.IsValid()) {
    LOGF(ERROR) << "Failed to create YUV shared image";
    task->Fail();
    metrics_.RecordError(CameraEffectError::kGPUImageInitializationFailed);
    return;
  }
  process_ctx.rgba_buffer = task->IsStillCapture()
                                ? still_rgba_buffer_pool_->RequestBuffer()
                                : video_rgba_buffer_pool_->RequestBuffer();
  if (!process_ctx.rgba_buffer) {
    LOGF(ERROR) << "Failed to allocate RGBA buffer";
    task->Fail();
    metrics_.RecordError(CameraEffectError::kBufferAllocationError);
    return;
  }
  process_ctx.rgba_image = SharedImage::CreateFromBuffer(
      *process_ctx.rgba_buffer->handle(), Texture2D::Target::kTarget2D,
      /*separate_yuv_textures=*/false);
  if (!process_ctx.rgba_image.IsValid()) {
    LOGF(ERROR) << "Failed to create RGBA shared image";
    task->Fail();
    metrics_.RecordError(CameraEffectError::kGPUImageInitializationFailed);
    return;
  }

  CHECK_EQ(process_ctx.yuv_image.y_texture().width(),
           process_ctx.rgba_image.texture().width());
  CHECK_EQ(process_ctx.yuv_image.y_texture().height(),
           process_ctx.rgba_image.texture().height());
  if (!image_processor_->NV12ToRGBA(process_ctx.yuv_image.y_texture(),
                                    process_ctx.yuv_image.uv_texture(),
                                    process_ctx.rgba_image.texture())) {
    LOGF(ERROR) << "Failed to convert from YUV to RGB";
    task->Fail();
    metrics_.RecordError(CameraEffectError::kYUVConversionFailed);
    return;
  }
  glFinish();

  auto [it, task_added] = tasks_.emplace(timestamp, std::move(task));
  CHECK(task_added);
  if (!pipeline_->ProcessFrame(timestamp,
                               process_ctx.rgba_image.texture().handle(),
                               process_ctx.rgba_image.texture().width(),
                               process_ctx.rgba_image.texture().height())) {
    // Error logs and metrics are added in
    // |effects_pipeline_tracker_.TrackDroppedFrame()|.
    it->second->Fail();
    tasks_.erase(it);
    effects_pipeline_tracker_.TrackDroppedFrame();
    return;
  }
}

void EffectsStreamManipulatorImpl::Notify(camera3_notify_msg_t msg) {
  helper_->Notify(msg);
}

bool EffectsStreamManipulatorImpl::Flush() {
  helper_->Flush();
  return true;
}

scoped_refptr<base::SingleThreadTaskRunner>
EffectsStreamManipulatorImpl::GetTaskRunner() {
  return gl_thread_.task_runner();
}

void EffectsStreamManipulatorImpl::OnFrameProcessed(int64_t timestamp,
                                                    GLuint texture,
                                                    uint32_t width,
                                                    uint32_t height) {
  TRACE_EFFECTS("timestamp", timestamp);
  effects_pipeline_tracker_.TrackProcessedFrame();

  // Synchronously wait until the texture is consumed before the pipeline
  // recycles it.
  gl_thread_.PostTaskSync(
      FROM_HERE, base::BindOnce(&EffectsStreamManipulatorImpl::PostProcess,
                                base::Unretained(this), timestamp, texture,
                                width, height));
}

void EffectsStreamManipulatorImpl::PostProcess(int64_t timestamp,
                                               GLuint texture,
                                               uint32_t width,
                                               uint32_t height) {
  DCHECK_CALLED_ON_VALID_THREAD(gl_thread_checker_);
  TRACE_EFFECTS("timestamp", timestamp);

  auto it = tasks_.find(timestamp);
  if (it == tasks_.end()) {
    LOGF(WARNING) << "Drop pipeline result at " << timestamp
                  << " since context is gone";
    return;
  }
  ScopedProcessTask task = std::move(it->second);
  tasks_.erase(it);

  auto& capture_ctx = *task->GetPrivateContextAs<CaptureContext>();
  auto& process_ctx = task->IsStillCapture()
                          ? capture_ctx.still_process_context
                          : capture_ctx.video_process_context;

  // The pipeline produces a GL texture, which needs to be synchronously
  // converted to YUV on this thread (because that's where the GL context
  // is bound). However, the pipeline must be prevented from recycling the
  // texture while the color space conversion is in progress. To facilitate
  // this, we:
  //
  // 1. Synchronously convert RGB to YUV.
  // 2. Unblock OnFrameProcessed to return the texture to the pipeline.
  //
  SharedImage out_yuv_image = SharedImage::CreateFromBuffer(
      task->output_buffer(), Texture2D::Target::kTarget2D,
      /*separate_yuv_textures=*/true);
  if (!out_yuv_image.IsValid()) {
    LOGF(ERROR) << "Failed to create YUV shared image";
    task->Fail();
    metrics_.RecordError(CameraEffectError::kGPUImageInitializationFailed);
    return;
  }
  CHECK_EQ(width, out_yuv_image.y_texture().width());
  CHECK_EQ(height, out_yuv_image.y_texture().height());
  Texture2D texture_2d(texture, kRGBAFormat, width, height);
  if (!image_processor_->RGBAToNV12(texture_2d, out_yuv_image.y_texture(),
                                    out_yuv_image.uv_texture())) {
    LOGF(ERROR) << "Failed to convert from RGB to YUV";
    texture_2d.Release();
    task->Fail();
    metrics_.RecordError(CameraEffectError::kYUVConversionFailed);
    return;
  }
  glFinish();
  texture_2d.Release();

  const auto process_end_timestamp = base::TimeTicks::Now();
  metrics_.RecordFrameProcessingLatency(
      capture_ctx.effects,
      task->IsStillCapture() ? CameraEffectStreamType::kBlob
                             : CameraEffectStreamType::kYuv,
      process_end_timestamp - process_ctx.start_time);
  if (task->IsStillCapture()) {
    metrics_.RecordStillShotTaken();
  }
  if (VLOG_IS_ON(1)) {
    LogAverageLatency(process_end_timestamp - process_ctx.start_time);
  }
}

void EffectsStreamManipulatorImpl::OnOptionsUpdated(
    const base::Value::Dict& json_values) {
  DCHECK_CALLED_ON_VALID_THREAD(gl_thread_checker_);
  LOGF(INFO) << "Reloadable Options update detected";
  CHECK(pipeline_);

  std::string default_segmentation_model;
  if (GetStringFromKey(json_values, kDefaultSegmentationModelTypeKey,
                       &default_segmentation_model)) {
    if (ParseSegmentationModelType(default_segmentation_model,
                                   default_segmentation_model_type_)) {
      LOG(INFO) << "Default segmentation model type set to "
                << default_segmentation_model;
    } else {
      LOG(WARNING) << "Model type " << default_segmentation_model
                   << " not recognized, keeping original default";
    }
  }

  override_config_exists_ = base::PathExists(kOverrideEffectsConfigFile);
  // The code after this point is only relevant if there is an
  // override file. Abort here so we don't set a 'default'
  // effects config.
  if (!override_config_exists_)
    return;

  EffectsConfig new_config;
  std::string effect_val;
  if (GetStringFromKey(json_values, kEffectKey, &effect_val)) {
    base::flat_map<std::string, bool*> effect_name_to_enabled = {
        {"blur", &new_config.blur_enabled},
        {"replace", &new_config.replace_enabled},
        {"relight", &new_config.relight_enabled},
        {"retouch", &new_config.face_retouch_enabled},
    };
    if (effect_val == "none") {
      for (auto& [unused, enabled] : effect_name_to_enabled) {
        *enabled = false;
      }
    } else {
      std::vector<std::string> effects = base::SplitString(
          effect_val, "_", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      for (const std::string& effect : effects) {
        auto it = effect_name_to_enabled.find(effect);
        if (it != effect_name_to_enabled.end()) {
          *it->second = true;
        } else {
          LOGF(WARNING) << "Unknown Effect: " << effect;
        }
      }
    }
    LOGF(INFO) << "Effect Updated: " << effect_val;
  }

  LoadIfExist(json_values, kBlurEnabled, &new_config.blur_enabled);
  LoadIfExist(json_values, kReplaceEnabled, &new_config.replace_enabled);
  LoadIfExist(json_values, kRelightEnabled, &new_config.relight_enabled);
  LoadIfExist(json_values, kRetouchEnabled, &new_config.face_retouch_enabled);

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

  double face_retouch_strength;
  if (GetDoubleFromKey(json_values, kRetouchStrength, &face_retouch_strength)) {
    new_config.face_retouch_strength = face_retouch_strength;
    LOGF(INFO) << "Retouch Strength: " << face_retouch_strength;
  }

  std::string delegate;
  if (GetStringFromKey(json_values, kDelegateKey, &delegate)) {
    if (delegate == "gpu") {
      new_config.segmentation_delegate = Delegate::kGpu;
      new_config.relighting_delegate = Delegate::kGpu;
    } else if (delegate == "stable") {
      new_config.segmentation_delegate = Delegate::kStable;
      new_config.relighting_delegate = Delegate::kStable;
    } else {
      LOGF(WARNING) << "Unknown Delegate: " << delegate;
      return;
    }
    LOG(INFO) << "Delegate: " << delegate;
  }

  std::string relighting_delegate;
  if (GetStringFromKey(json_values, kRelightingDelegateKey,
                       &relighting_delegate)) {
    if (delegate == "gpu") {
      new_config.relighting_delegate = Delegate::kGpu;
    } else if (delegate == "stable") {
      new_config.relighting_delegate = Delegate::kStable;
    } else {
      LOGF(WARNING) << "Unknown Delegate: " << delegate;
      return;
    }
    LOGF(INFO) << "Delegate: " << delegate;
  }

  if (new_config.segmentation_delegate == Delegate::kGpu ||
      new_config.relighting_delegate == Delegate::kGpu) {
    std::string gpu_api;
    if (GetStringFromKey(json_values, kGpuApiKey, &gpu_api)) {
      if (gpu_api == "opengl") {
        new_config.segmentation_gpu_api = GpuApi::kOpenGL;
        new_config.relighting_gpu_api = GpuApi::kOpenGL;
      } else if (gpu_api == "opencl") {
        new_config.segmentation_gpu_api = GpuApi::kOpenCL;
        new_config.relighting_gpu_api = GpuApi::kOpenCL;
      } else if (gpu_api == "vulkan") {
        new_config.segmentation_gpu_api = GpuApi::kVulkan;
        // Relighting stays as OpenCL in the Vulkan case
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
  }

  if (new_config.relighting_delegate == Delegate::kGpu) {
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
        LOGF(WARNING) << "Unknown Relighting GPU API: " << relighting_gpu_api;
        return;
      }
      LOGF(INFO) << "Relighting GPU API: " << relighting_gpu_api;
    }
  }

  if (new_config.segmentation_delegate == Delegate::kStable ||
      new_config.relighting_delegate == Delegate::kStable) {
    std::string stable_delegate_settings_file;
    if (!GetStringFromKey(json_values, kStableDelegateSettingsFileKey,
                          &stable_delegate_settings_file)) {
      stable_delegate_settings_file = kTfliteStableDelegateSettingsFile;
    }
    if (stable_delegate_settings_file.size() >=
        sizeof(new_config.stable_delegate_settings_file)) {
      LOGF(WARNING) << "Stable Delegate Settings File Path too long.";
      return;
    }
    base::strlcpy(new_config.stable_delegate_settings_file,
                  stable_delegate_settings_file.c_str(),
                  sizeof(new_config.stable_delegate_settings_file));
    LOGF(INFO) << "Stable Delegate Settings File: "
               << stable_delegate_settings_file;
  }

  std::string segmentation_model_type;
  if (GetStringFromKey(json_values, kSegmentationModelTypeKey,
                       &segmentation_model_type)) {
    if (ParseSegmentationModelType(segmentation_model_type,
                                   new_config.segmentation_model_type)) {
      LOGF(INFO) << "Segmentation Model Type: " << segmentation_model_type;
      if (new_config.segmentation_model_type == SegmentationModelType::kAuto) {
        LOGF(INFO) << "Using segmentation model type: "
                   << static_cast<int>(default_segmentation_model_type_);
        new_config.segmentation_model_type = default_segmentation_model_type_;
      }
    }
  }

  // Only apply the effect if something changed, as sometimes this function
  // can get called several times after one file save which is expensive.
  if (new_config != last_set_effect_config_) {
    SetEffect(std::move(new_config));
  }
}

void EffectsStreamManipulatorImpl::SetEffect(EffectsConfig new_config) {
  DCHECK_CALLED_ON_VALID_THREAD(gl_thread_checker_);
  CHECK(pipeline_);
  // The maximum number of in-flight frames is determined in this SM.
  CHECK_GT(graph_max_frames_in_flight_, 0);
  new_config.graph_max_frames_in_flight =
      base::checked_cast<int>(graph_max_frames_in_flight_);

  pipeline_->SetEffect(&new_config, set_effect_callback_);
  last_set_effect_config_ = new_config;

  if (new_config.HasEnabledEffects()) {
    metrics_.RecordSelectedEffect(new_config);
  }
}

bool EffectsStreamManipulatorImpl::SetupGlThread(
    base::FilePath config_file_path) {
  DCHECK_CALLED_ON_VALID_THREAD(gl_thread_checker_);
  TRACE_EFFECTS();

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

void EffectsStreamManipulatorImpl::CreatePipeline(
    const base::FilePath& dlc_root_path) {
  DCHECK_CALLED_ON_VALID_THREAD(gl_thread_checker_);
  // Check to see if the cache dir is empty, and if so,
  // point the pipeline to the prebuilt cache as this may
  // indicate the opencl_cacher tool hasn't had the chance
  // to run or complete yet. Niche edge case, but it does
  // cause a large startup delay for the user. This is particularly
  // true when running behind a chrome flag, as the cacher
  // tool doesn't run on a UI restart.
  base::FilePath cache_dir_override("");
  // Don't override the cache if the marker file exists,
  // since we may be trying to recover from a bad cache.
  if (!base::PathExists(kEffectsRunningMarker)) {
    auto default_cache_dir = base::FilePath(kOpenCLCachingDir);
    if (DirIsEmpty(default_cache_dir)) {
      cache_dir_override = PrebuiltCacheDir(dlc_root_path);
      LOGF(INFO) << "OpenCL cache at " << default_cache_dir
                 << " is empty, using " << cache_dir_override << " instead.";
    }
  }

  marker_file_timer_ = CreateEffectsMarkerFile();

  pipeline_ = EffectsPipeline::Create(dlc_root_path, egl_context_->Get(),
                                      cache_dir_override);
  pipeline_->SetRenderedImageObserver(std::make_unique<RenderedImageObserver>(
      base::BindRepeating(&EffectsStreamManipulatorImpl::OnFrameProcessed,
                          base::Unretained(this))));

  config_ =
      std::make_unique<ReloadableConfigFile>(ReloadableConfigFile::Options{
          .default_config_file_path = config_file_path_,
          .override_config_file_path = kOverrideEffectsConfigFile,
      });
  if (!config_->IsValid()) {
    LOGF(WARNING) << "Cannot load valid JSON config";
  }
  config_->SetCallback(base::BindRepeating(
      &EffectsStreamManipulatorImpl::OnOptionsUpdated, base::Unretained(this)));
}

void EffectsStreamManipulatorImpl::UploadAndResetMetricsData() {
  EffectsMetricsData metrics_copy(metrics_);
  metrics_ = EffectsMetricsData();
  metrics_uploader_->UploadMetricsData(std::move(metrics_copy));
}

}  // namespace cros
