/*
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/hdrnet_stream_manipulator.h"

#include <hardware/camera3.h>
#include <sync/sync.h>
#include <system/camera_metadata.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/containers/lru_cache.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>

#include "common/camera_hal3_helpers.h"
#include "common/stream_manipulator.h"
#include "common/stream_manipulator_helper.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metrics.h"
#include "cros-camera/common.h"
#include "cros-camera/spatiotemporal_denoiser.h"
#include "cros-camera/texture_2d_descriptor.h"
#include "features/hdrnet/hdrnet_config.h"
#include "features/hdrnet/hdrnet_processor_device_adapter.h"
#include "features/hdrnet/hdrnet_processor_impl.h"
#include "features/hdrnet/tracing.h"
#include "gpu/gles/texture_2d.h"
#include "gpu/gpu_resources.h"

namespace cros {

namespace {

constexpr int kDefaultSyncWaitTimeoutMs = 300;

constexpr char kMetadataDumpPath[] = "/run/camera/hdrnet_frame_metadata.json";

constexpr char kLogFrameMetadata[] = "log_frame_metadata";

constexpr char kDenoiserEnable[] = "denoiser_enable";
constexpr char kDenoiserIirTemporalConvergence[] =
    "denoiser_iir_temporal_convergence";
constexpr char kDenoiserNumSpatialPasses[] = "num_spatial_passes";
constexpr char kDenoiserSpatialStrength[] = "spatial_strength";

// Used for caching the persistent HDRnet GpuResources instance across camera
// sessions in the root GpuResources instance.
class CachedHdrNetGpuResources : public GpuResources::CacheContainer {
 public:
  constexpr static char kCachedHdrNetGpuResourcesId[] =
      "hdrnet.hdrnet_gpu_resources";

  CachedHdrNetGpuResources() = default;
  ~CachedHdrNetGpuResources() override = default;

  GpuResources* GetHdrNetGpuResources() const {
    return hdrnet_gpu_resources_.get();
  }

  void CreateHdrNetGpuResources(GpuResources* root_gpu_resources) {
    hdrnet_gpu_resources_ = std::make_unique<GpuResources>(GpuResourcesOptions{
        .name = "HdrNetGpuResources", .shared_resources = root_gpu_resources});
    CHECK(hdrnet_gpu_resources_->Initialize());
  }

 private:
  std::unique_ptr<GpuResources> hdrnet_gpu_resources_;
};

// Used for caching the pipeline resources across camera sessions in the
// persistent HDRnet GpuResources instance.
class CachedPipelineResources : public GpuResources::CacheContainer {
 public:
  constexpr static char kCachedPipelineResourcesId[] = "hdrnet.cached_pipeline";
  constexpr static size_t kMaxCacheSize = 5;

  CachedPipelineResources()
      : processors_(kMaxCacheSize), denoisers_(kMaxCacheSize) {}

  ~CachedPipelineResources() override = default;

  // HDRnet processor is stateless. Its internal buffers are initialized
  // according to the input image size. We can cache, share and reuse the HDRnet
  // processor of the same size across different streams or device sessions.
  HdrNetProcessor* GetProcessor(const Size& input_size) {
    auto it = processors_.Get(input_size);
    if (it == processors_.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  void PutProcessor(const Size input_size,
                    std::unique_ptr<HdrNetProcessor> processor) {
    DCHECK(processors_.Peek(input_size) == processors_.end());
    processors_.Put(input_size, std::move(processor));
  }

  // The Spatiotemporal denoiser initializes its internal buffers according to
  // the size of the input image. The internal IIR filter is stateful, but as
  // long as we reset the IIR filter every time we start a new stream, we can
  // cache and reuse the denoisers.
  //
  // TODO(jcliang): We might need to separate the denoisers of two streams with
  // the same resolution for some use-caes.
  SpatiotemporalDenoiser* GetDenoiser(const Size& input_size) {
    auto it = denoisers_.Get(input_size);
    if (it == denoisers_.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  void PutDenoiser(const Size input_size,
                   std::unique_ptr<SpatiotemporalDenoiser> denoiser) {
    DCHECK(denoisers_.Peek(input_size) == denoisers_.end());
    denoisers_.Put(input_size, std::move(denoiser));
  }

 private:
  base::HashingLRUCache<Size, std::unique_ptr<HdrNetProcessor>> processors_;
  base::HashingLRUCache<Size, std::unique_ptr<SpatiotemporalDenoiser>>
      denoisers_;
};

}  // namespace

//
// HdrNetStreamManipulator implementations.
//

HdrNetStreamManipulator::HdrNetStreamManipulator(
    RuntimeOptions* runtime_options,
    GpuResources* root_gpu_resources,
    base::FilePath config_file_path,
    std::string camera_module_name,
    std::unique_ptr<StillCaptureProcessor> still_capture_processor,
    HdrNetProcessor::Factory hdrnet_processor_factory,
    HdrNetConfig::Options* options)
    : runtime_options_(runtime_options),
      root_gpu_resources_(root_gpu_resources),
      hdrnet_processor_factory_(
          !hdrnet_processor_factory.is_null()
              ? std::move(hdrnet_processor_factory)
              : base::BindRepeating(HdrNetProcessorImpl::CreateInstance)),
      config_(ReloadableConfigFile::Options{
          config_file_path,
          base::FilePath(HdrNetConfig::kOverrideHdrNetConfigFile)}),
      camera_module_name_(std::move(camera_module_name)),
      still_capture_processor_(std::move(still_capture_processor)),
      camera_metrics_(CameraMetrics::New()),
      metadata_logger_({.dump_path = base::FilePath(kMetadataDumpPath)}) {
  DCHECK(root_gpu_resources_);
  root_gpu_resources_->PostGpuTaskSync(
      FROM_HERE,
      base::BindOnce(
          &HdrNetStreamManipulator::InitializeGpuResourcesOnRootGpuThread,
          base::Unretained(this)));
  CHECK_NE(hdrnet_gpu_resources_, nullptr);

  if (!config_.IsValid()) {
    if (options) {
      // Options for testing.
      options_ = *options;
    } else {
      LOGF(ERROR) << "Cannot load valid config; turn off feature by default";
      options_.hdrnet_enable = false;
    }
  }
  config_.SetCallback(base::BindRepeating(
      &HdrNetStreamManipulator::OnOptionsUpdated, base::Unretained(this)));
}

HdrNetStreamManipulator::~HdrNetStreamManipulator() {
  hdrnet_gpu_resources_->PostGpuTaskSync(
      FROM_HERE, base::BindOnce(&HdrNetStreamManipulator::ResetStateOnGpuThread,
                                base::Unretained(this)));
}

bool HdrNetStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  DCHECK(hdrnet_gpu_resources_);

  bool ret;
  hdrnet_gpu_resources_->PostGpuTaskSync(
      FROM_HERE,
      base::BindOnce(&HdrNetStreamManipulator::InitializeOnGpuThread,
                     base::Unretained(this), base::Unretained(static_info),
                     std::move(callbacks)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  DCHECK(hdrnet_gpu_resources_);

  bool ret;
  hdrnet_gpu_resources_->PostGpuTaskSync(
      FROM_HERE,
      base::BindOnce(&HdrNetStreamManipulator::ConfigureStreamsOnGpuThread,
                     base::Unretained(this), base::Unretained(stream_config)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  DCHECK(hdrnet_gpu_resources_);

  bool ret;
  hdrnet_gpu_resources_->PostGpuTaskSync(
      FROM_HERE,
      base::BindOnce(&HdrNetStreamManipulator::OnConfiguredStreamsOnGpuThread,
                     base::Unretained(this), base::Unretained(stream_config)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  return true;
}

bool HdrNetStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  DCHECK(hdrnet_gpu_resources_);

  bool ret;
  hdrnet_gpu_resources_->PostGpuTaskSync(
      FROM_HERE,
      base::BindOnce(&HdrNetStreamManipulator::ProcessCaptureRequestOnGpuThread,
                     base::Unretained(this), base::Unretained(request)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor result) {
  DCHECK(hdrnet_gpu_resources_);

  hdrnet_gpu_resources_->PostGpuTask(
      FROM_HERE,
      base::BindOnce(&HdrNetStreamManipulator::ProcessCaptureResultOnGpuThread,
                     base::Unretained(this), std::move(result)));
  return true;
}

void HdrNetStreamManipulator::Notify(camera3_notify_msg_t msg) {
  DCHECK(hdrnet_gpu_resources_);

  bool ret;
  hdrnet_gpu_resources_->PostGpuTaskSync(
      FROM_HERE,
      base::BindOnce(&HdrNetStreamManipulator::NotifyOnGpuThread,
                     base::Unretained(this), base::Unretained(&msg)),
      &ret);
}

bool HdrNetStreamManipulator::Flush() {
  DCHECK(hdrnet_gpu_resources_);

  bool ret;
  hdrnet_gpu_resources_->PostGpuTaskSync(
      FROM_HERE,
      base::BindOnce(&HdrNetStreamManipulator::FlushOnGpuThread,
                     base::Unretained(this)),
      &ret);
  return ret;
}

void HdrNetStreamManipulator::InitializeGpuResourcesOnRootGpuThread() {
  DCHECK(root_gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());

  CachedHdrNetGpuResources* cache =
      root_gpu_resources_->GetCache<CachedHdrNetGpuResources>(
          CachedHdrNetGpuResources::kCachedHdrNetGpuResourcesId);
  if (!cache) {
    root_gpu_resources_->SetCache(
        CachedHdrNetGpuResources::kCachedHdrNetGpuResourcesId,
        std::make_unique<CachedHdrNetGpuResources>());
    cache = root_gpu_resources_->GetCache<CachedHdrNetGpuResources>(
        CachedHdrNetGpuResources::kCachedHdrNetGpuResourcesId);
  }
  CHECK(cache);

  if (!cache->GetHdrNetGpuResources()) {
    cache->CreateHdrNetGpuResources(root_gpu_resources_);
  }
  hdrnet_gpu_resources_ = cache->GetHdrNetGpuResources();
}

bool HdrNetStreamManipulator::InitializeOnGpuThread(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  DCHECK(hdrnet_gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_HDRNET();

  static_info_.acquire(clone_camera_metadata(static_info));
  helper_ = std::make_unique<StreamManipulatorHelper>(
      StreamManipulatorHelper::Config{
          .process_mode = ProcessMode::kVideoAndStillProcess,
          .result_metadata_tags_to_update =
              HdrNetProcessorDeviceAdapter::GetResultMetadataTagsOfInterest(),
      },
      camera_module_name_, static_info, std::move(callbacks),
      base::BindRepeating(&HdrNetStreamManipulator::OnProcessTask,
                          base::Unretained(this)),
      GetCropScaleImageCallback(hdrnet_gpu_resources_->gpu_task_runner(),
                                hdrnet_gpu_resources_->image_processor()),
      std::move(still_capture_processor_),
      hdrnet_gpu_resources_->gpu_task_runner());
  return true;
}

bool HdrNetStreamManipulator::ConfigureStreamsOnGpuThread(
    Camera3StreamConfiguration* stream_config) {
  DCHECK(hdrnet_gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_HDRNET([&](perfetto::EventContext ctx) {
    stream_config->PopulateEventAnnotation(ctx);
  });

  // Clear the stream configuration from the previous session.
  ResetStateOnGpuThread();

  if (!helper_->PreConfigure(stream_config)) {
    return false;
  }

  hdrnet_metrics_.num_concurrent_hdrnet_streams = 0;
  if (const camera3_stream_t* s = helper_->video_process_input_stream()) {
    hdrnet_stream_context_[s] = std::make_unique<HdrNetStreamContext>();
    hdrnet_metrics_.stream_config = HdrnetStreamConfiguration::kSingleYuvStream;
    hdrnet_metrics_.max_yuv_stream_size = s->width * s->height;
    ++hdrnet_metrics_.num_concurrent_hdrnet_streams;
  }
  if (const camera3_stream_t* s = helper_->still_process_input_stream()) {
    hdrnet_stream_context_[s] = std::make_unique<HdrNetStreamContext>();
    hdrnet_metrics_.stream_config =
        HdrnetStreamConfiguration::kSingleYuvStreamWithBlob;
    hdrnet_metrics_.max_blob_stream_size = s->width * s->height;
    ++hdrnet_metrics_.num_concurrent_hdrnet_streams;
  }

  return true;
}

bool HdrNetStreamManipulator::OnConfiguredStreamsOnGpuThread(
    Camera3StreamConfiguration* stream_config) {
  DCHECK(hdrnet_gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_HDRNET([&](perfetto::EventContext ctx) {
    stream_config->PopulateEventAnnotation(ctx);
  });

  helper_->PostConfigure(stream_config);

  bool success = SetUpPipelineOnGpuThread();
  if (!success) {
    LOGF(ERROR) << "Cannot set up HDRnet pipeline";
    return false;
  }

  return true;
}

bool HdrNetStreamManipulator::ProcessCaptureRequestOnGpuThread(
    Camera3CaptureDescriptor* request) {
  DCHECK(hdrnet_gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_HDRNET("frame_number", request->frame_number());

  bool skip_hdrnet_processing = false;
  base::span<const uint8_t> tm_mode =
      request->GetMetadata<uint8_t>(ANDROID_TONEMAP_MODE);
  if (!tm_mode.empty() && (tm_mode[0] == ANDROID_TONEMAP_MODE_CONTRAST_CURVE ||
                           tm_mode[0] == ANDROID_TONEMAP_MODE_GAMMA_VALUE ||
                           tm_mode[0] == ANDROID_TONEMAP_MODE_PRESET_CURVE)) {
    skip_hdrnet_processing = true;
  }

  if (runtime_options_->sw_privacy_switch_state() ==
      mojom::CameraPrivacySwitchState::ON) {
    skip_hdrnet_processing = true;
  }

  for (auto& [stream, context] : hdrnet_stream_context_) {
    context->processor->SetOptions(
        {.metadata_logger =
             options_.log_frame_metadata ? &metadata_logger_ : nullptr});
  }

  helper_->HandleRequest(request, skip_hdrnet_processing, nullptr);

  for (auto& buffer : request->GetOutputBuffers()) {
    if (!hdrnet_stream_context_.contains(buffer.stream())) {
      continue;
    }
    HdrNetStreamContext* stream_context =
        hdrnet_stream_context_[buffer.stream()].get();

    // Only change the metadata when the client request settings is not null.
    // This is mainly to make the CTS tests happy, as some test cases set null
    // settings and if we change that the vendor camera HAL may not handle the
    // incremental changes well.
    if (request->has_metadata()) {
      stream_context->processor->WriteRequestParameters(request);
    }
  }

  return true;
}

bool HdrNetStreamManipulator::ProcessCaptureResultOnGpuThread(
    Camera3CaptureDescriptor result) {
  DCHECK(hdrnet_gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_HDRNET("frame_number", result.frame_number());

  helper_->HandleResult(std::move(result));
  return true;
}

void HdrNetStreamManipulator::OnProcessTask(ScopedProcessTask task) {
  DCHECK(hdrnet_gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_HDRNET("frame_number", task->frame_number());

  TRACE_HDRNET_EVENT(
      "HdrNetStreamManipulator::ProcessHdrnetBuffer", "frame_number",
      task->frame_number(), "width", task->input_stream()->width, "height",
      task->input_stream()->height, "format", task->input_stream()->format,
      perfetto::Flow::ProcessScoped(task->flow_id()));
  HdrNetStreamContext* stream_context =
      hdrnet_stream_context_[task->input_stream()].get();
  CHECK_NE(stream_context, nullptr);

  if (options_.hdrnet_enable) {
    // TODO(jcliang): Update the LUT textures once and share it with all
    // processors.
    stream_context->processor->ProcessResultMetadata(task->frame_number(),
                                                     task->result_metadata());
  }
  std::optional<base::Value::Dict> overridden_json_values =
      HdrNetProcessorDeviceAdapter::MaybeOverrideOptions(
          json_values_, task->result_metadata(), override_data_);
  if (overridden_json_values.has_value()) {
    SetOptions(*overridden_json_values);
  }

  // TODO(kamesan): Cache the shared images on the input buffers allocated in
  // the stream manipulator helper.
  SharedImage input_img = SharedImage::CreateFromBuffer(
      task->input_buffer(), Texture2D::Target::kTarget2D,
      /*separate_yuv_textures=*/true);
  if (!input_img.y_texture().IsValid() || !input_img.uv_texture().IsValid()) {
    LOGF(ERROR) << "Cannot create SharedImage for the HDRnet buffer";
    task->Fail();
    ++hdrnet_metrics_.errors[HdrnetError::kInitializationError];
    return;
  }

  if (options_.denoiser_enable) {
    TRACE_HDRNET_EVENT("HdrNetStreamManipulator::RunIirDenoise",
                       perfetto::Flow::ProcessScoped(task->flow_id()));
    // Run the denoiser.
    Texture2DDescriptor input_luma = {
        .id = static_cast<GLint>(input_img.y_texture().handle()),
        .internal_format = input_img.y_texture().internal_format(),
        .width = input_img.y_texture().width(),
        .height = input_img.y_texture().height(),
    };
    Texture2DDescriptor input_chroma = {
        .id = static_cast<GLint>(input_img.uv_texture().handle()),
        .internal_format = input_img.uv_texture().internal_format(),
        .width = input_img.uv_texture().width(),
        .height = input_img.uv_texture().height(),
    };

    SharedImage& output_img = stream_context->denoiser_intermediate;
    Texture2DDescriptor output_luma = {
        .id = static_cast<GLint>(output_img.y_texture().handle()),
        .internal_format = output_img.y_texture().internal_format(),
        .width = output_img.y_texture().width(),
        .height = output_img.y_texture().height(),
    };
    Texture2DDescriptor output_chroma = {
        .id = static_cast<GLint>(output_img.uv_texture().handle()),
        .internal_format = output_img.uv_texture().internal_format(),
        .width = output_img.uv_texture().width(),
        .height = output_img.uv_texture().height(),
    };
    stream_context->denoiser->RunIirDenoise(
        input_luma, input_chroma, output_luma, output_chroma,
        {.iir_temporal_convergence = options_.iir_temporal_convergence,
         .spatial_strength = options_.spatial_strength,
         .num_spatial_passes = options_.num_spatial_passes,
         .reset_temporal_buffer =
             stream_context->should_reset_temporal_buffer});
    if (stream_context->should_reset_temporal_buffer) {
      stream_context->should_reset_temporal_buffer = false;
    }
  }

  base::ScopedFD output_acquire_fence = task->TakeOutputAcquireFence();
  if (output_acquire_fence.is_valid() &&
      sync_wait(output_acquire_fence.get(), kDefaultSyncWaitTimeoutMs) != 0) {
    LOGF(WARNING) << "sync_wait timeout on acquiring requested buffer";
    task->Fail();
    ++hdrnet_metrics_.errors[HdrnetError::kSyncWaitError];
    return;
  }

  // Run the HDRNet pipeline and write to the buffers.
  HdrNetConfig::Options processor_config =
      PrepareProcessorConfig(task->frame_number(), task->feature_metadata(),
                             /*skip_hdrnet_processing=*/false);
  const SharedImage& image = options_.denoiser_enable
                                 ? stream_context->denoiser_intermediate
                                 : input_img;
  base::ScopedFD release_fence = stream_context->processor->Run(
      task->frame_number(), processor_config, image,
      base::ScopedFD(task->TakeInputReleaseFence()), {task->output_buffer()},
      &hdrnet_metrics_);
  task->SetOutputReleaseFence(std::move(release_fence));

  hdrnet_metrics_.max_output_buffers_rendered = 1;
  if (task->IsStillCapture()) {
    ++hdrnet_metrics_.num_still_shot_taken;
  }
}

bool HdrNetStreamManipulator::NotifyOnGpuThread(camera3_notify_msg_t* msg) {
  DCHECK(hdrnet_gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_HDRNET();

  if (msg->type == CAMERA3_MSG_ERROR) {
    ++hdrnet_metrics_.errors[HdrnetError::kCameraHal3Error];
  }

  helper_->Notify(*msg);
  return true;
}

bool HdrNetStreamManipulator::FlushOnGpuThread() {
  DCHECK(hdrnet_gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_HDRNET();

  helper_->Flush();
  return true;
}

HdrNetConfig::Options HdrNetStreamManipulator::PrepareProcessorConfig(
    uint32_t frame_number,
    const FeatureMetadata& feature_metadata,
    bool skip_hdrnet_processing) const {
  // Run the HDRNet pipeline and write to the buffers.
  HdrNetConfig::Options run_options = options_;

  // Use the HDR ratio calculated by Gcam AE if available.
  std::optional<float> gcam_ae_hdr_ratio = feature_metadata.hdr_ratio;
  if (gcam_ae_hdr_ratio) {
    run_options.hdr_ratio = *feature_metadata.hdr_ratio;
    DVLOGFID(1, frame_number) << "Using HDR ratio=" << run_options.hdr_ratio;
  }

  // Disable HDRnet processing completely if the tonemap mode is set to contrast
  // curve, gamma value, or preset curve.
  if (skip_hdrnet_processing) {
    run_options.hdrnet_enable = false;
    DVLOGFID(1, frame_number) << "Disable HDRnet processing";
  }

  return run_options;
}

bool HdrNetStreamManipulator::SetUpPipelineOnGpuThread() {
  DCHECK(hdrnet_gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_HDRNET();

  CachedPipelineResources* cache =
      hdrnet_gpu_resources_->GetCache<CachedPipelineResources>(
          CachedPipelineResources::kCachedPipelineResourcesId);
  if (!cache) {
    hdrnet_gpu_resources_->SetCache(
        CachedPipelineResources::kCachedPipelineResourcesId,
        std::make_unique<CachedPipelineResources>());
    cache = hdrnet_gpu_resources_->GetCache<CachedPipelineResources>(
        CachedPipelineResources::kCachedPipelineResourcesId);
  }
  CHECK(cache);

  const camera_metadata_t* locked_static_info = static_info_.getAndLock();
  for (const auto& [stream, context] : hdrnet_stream_context_) {
    TRACE_HDRNET_EVENT("HdrNetStreamManipulator::SetUpContextResources",
                       "width", stream->width, "height", stream->height);
    Size stream_size(stream->width, stream->height);

    {
      TRACE_HDRNET_EVENT("HdrNetStreamManipulator::CreateHdrnetProcessor");
      context->processor = cache->GetProcessor(stream_size);
      if (!context->processor) {
        cache->PutProcessor(
            stream_size,
            hdrnet_processor_factory_.Run(
                locked_static_info, hdrnet_gpu_resources_->gpu_task_runner()));
        context->processor = cache->GetProcessor(stream_size);
        if (!context->processor) {
          LOGF(ERROR) << "Failed to initialize HDRnet processor";
          ++hdrnet_metrics_.errors[HdrnetError::kInitializationError];
          return false;
        }
        context->processor->Initialize(hdrnet_gpu_resources_, stream_size,
                                       {stream_size});
      }
    }

    {
      TRACE_HDRNET_EVENT("HdrNetStreamManipulator::CreateDenoiser");
      context->denoiser = cache->GetDenoiser(stream_size);
      if (!context->denoiser) {
        cache->PutDenoiser(
            stream_size,
            SpatiotemporalDenoiser::CreateInstance(
                {.frame_width = static_cast<int>(stream_size.width),
                 .frame_height = static_cast<int>(stream_size.height),
                 .mode = SpatiotemporalDenoiser::Mode::kIirMode}));
        context->denoiser = cache->GetDenoiser(stream_size);
        if (!context->denoiser) {
          LOGF(ERROR) << "Failed to initialize Spatiotemporal denoiser";
          ++hdrnet_metrics_.errors[HdrnetError::kInitializationError];
          return false;
        }
      }
    }

    TRACE_HDRNET_BEGIN("HdrNetStreamManipulator::AllocateIntermediateBuffers");

    {
      constexpr uint32_t kBufferUsage =
          GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_TEXTURE;
      ScopedBufferHandle buffer = CameraBufferManager::AllocateScopedBuffer(
          stream->width, stream->height, stream->format, kBufferUsage);
      if (!buffer) {
        LOGF(ERROR) << "Cannot allocate denoiser intermediate buffer";
        return false;
      }
      SharedImage shared_image = SharedImage::CreateFromBuffer(
          *buffer, Texture2D::Target::kTarget2D, true);
      if (!shared_image.y_texture().IsValid() ||
          !shared_image.uv_texture().IsValid()) {
        LOGF(ERROR)
            << "Cannot create SharedImage for the denoiser intermediate buffer";
        return false;
      }
      // Let the SharedImage own the buffer.
      shared_image.SetDestructionCallback(
          base::BindOnce([](ScopedBufferHandle buffer) {}, std::move(buffer)));
      context->denoiser_intermediate = std::move(shared_image);
    }

    TRACE_HDRNET_END();
  }
  static_info_.unlock(locked_static_info);

  return true;
}

void HdrNetStreamManipulator::ResetStateOnGpuThread() {
  CHECK(hdrnet_gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_HDRNET();

  for (auto& [s, ctx] : hdrnet_stream_context_) {
    if (ctx->processor) {
      ctx->processor->TearDown();
    }
  }
  hdrnet_stream_context_.clear();

  UploadMetrics();
  hdrnet_metrics_ = HdrnetMetrics();
}

void HdrNetStreamManipulator::OnOptionsUpdated(
    const base::Value::Dict& json_values) {
  json_values_ = json_values.Clone();
  SetOptions(HdrNetProcessorDeviceAdapter::GetOverriddenOptions(
      json_values, override_data_));
}

void HdrNetStreamManipulator::SetOptions(const base::Value::Dict& json_values) {
  ParseHdrnetJsonOptions(json_values, options_);

  bool denoiser_enable;
  if (LoadIfExist(json_values, kDenoiserEnable, &denoiser_enable)) {
    if (!options_.denoiser_enable && denoiser_enable) {
      // Reset the denoiser temporal buffer whenever we switch on the denoiser
      // to avoid artifacts caused by stale data.
      for (auto& [s, c] : hdrnet_stream_context_) {
        c->should_reset_temporal_buffer = true;
      }
    }
    options_.denoiser_enable = denoiser_enable;
  }
  LoadIfExist(json_values, kDenoiserIirTemporalConvergence,
              &options_.iir_temporal_convergence);
  LoadIfExist(json_values, kDenoiserNumSpatialPasses,
              &options_.num_spatial_passes);
  LoadIfExist(json_values, kDenoiserSpatialStrength,
              &options_.spatial_strength);

  bool log_frame_metadata;
  if (LoadIfExist(json_values, kLogFrameMetadata, &log_frame_metadata)) {
    if (options_.log_frame_metadata && !log_frame_metadata) {
      // Dump frame metadata when metadata logging if turned off.
      metadata_logger_.DumpMetadata();
      metadata_logger_.Clear();
    }
    options_.log_frame_metadata = log_frame_metadata;
  }

  DVLOGF(1) << "HDRnet config:" << " hdrnet_enable=" << options_.hdrnet_enable
            << " dump_buffer=" << options_.dump_buffer
            << " log_frame_metadata=" << options_.log_frame_metadata
            << " hdr_ratio=" << options_.hdr_ratio
            << " max_gain_blend_threshold=" << options_.max_gain_blend_threshold
            << " spatial_filter_sigma=" << options_.spatial_filter_sigma
            << " range_filter_sigma=" << options_.range_filter_sigma
            << " iir_filter_strength=" << options_.iir_filter_strength;
}

void HdrNetStreamManipulator::UploadMetrics() {
  if (hdrnet_metrics_.errors.empty() &&
      (hdrnet_metrics_.num_concurrent_hdrnet_streams == 0 ||
       hdrnet_metrics_.num_frames_processed == 0)) {
    // Avoid uploading metrics short-lived session that does not really do
    // anything. Short-lived session can happen when we first open a camera,
    // where the framework and the HAL may re-configure the streams more than
    // once.
    return;
  }
  camera_metrics_->SendHdrnetStreamConfiguration(hdrnet_metrics_.stream_config);
  camera_metrics_->SendHdrnetMaxStreamSize(HdrnetStreamType::kYuv,
                                           hdrnet_metrics_.max_yuv_stream_size);
  camera_metrics_->SendHdrnetMaxStreamSize(
      HdrnetStreamType::kBlob, hdrnet_metrics_.max_blob_stream_size);
  camera_metrics_->SendHdrnetNumConcurrentStreams(
      hdrnet_metrics_.num_concurrent_hdrnet_streams);
  camera_metrics_->SendHdrnetMaxOutputBuffersRendered(
      hdrnet_metrics_.max_output_buffers_rendered);
  camera_metrics_->SendHdrnetNumStillShotsTaken(
      hdrnet_metrics_.num_still_shot_taken);

  if (hdrnet_metrics_.errors.empty()) {
    camera_metrics_->SendHdrnetError(HdrnetError::kNoError);
  } else {
    for (auto [e, c] : hdrnet_metrics_.errors) {
      if (e == HdrnetError::kNoError) {
        NOTREACHED();
        continue;
      }
      if (c > 0) {
        // Since we want to normalize all our metrics by camera sessions, we
        // only report whether an type of error is happened and print the number
        // of error occurrences as error.
        LOGF(ERROR) << "There were " << c << " occurrences of error "
                    << static_cast<int>(e);
        camera_metrics_->SendHdrnetError(e);
      }
    }
  }

  if (hdrnet_metrics_.num_frames_processed > 0) {
    camera_metrics_->SendHdrnetAvgLatency(
        HdrnetProcessingType::kPreprocessing,
        hdrnet_metrics_.accumulated_preprocessing_latency_us /
            hdrnet_metrics_.num_frames_processed);
    camera_metrics_->SendHdrnetAvgLatency(
        HdrnetProcessingType::kRgbPipeline,
        hdrnet_metrics_.accumulated_rgb_pipeline_latency_us /
            hdrnet_metrics_.num_frames_processed);
    camera_metrics_->SendHdrnetAvgLatency(
        HdrnetProcessingType::kPostprocessing,
        hdrnet_metrics_.accumulated_postprocessing_latency_us /
            hdrnet_metrics_.num_frames_processed);
  }
}

}  // namespace cros
