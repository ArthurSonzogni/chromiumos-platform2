/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/hdrnet_stream_manipulator.h"

#include <algorithm>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <sync/sync.h>
#include <system/camera_metadata.h>

#include "common/still_capture_processor_impl.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/common.h"
#include "cros-camera/texture_2d_descriptor.h"
#include "features/hdrnet/hdrnet_processor_impl.h"
#include "gpu/egl/egl_fence.h"
#include "gpu/egl/utils.h"
#include "gpu/gles/texture_2d.h"
#include "gpu/gles/utils.h"

namespace cros {

namespace {

constexpr int kDefaultSyncWaitTimeoutMs = 300;

constexpr char kMetadataDumpPath[] = "/run/camera/hdrnet_frame_metadata.json";

constexpr char kDumpBufferKey[] = "dump_buffer";
constexpr char kHdrNetEnableKey[] = "hdrnet_enable";
constexpr char kLogFrameMetadataKey[] = "log_frame_metadata";

constexpr char kHdrRatioKey[] = "hdr_ratio";
constexpr char kMaxGainBlendThresholdKey[] = "max_gain_blend_threshold";
constexpr char kSpatialFilterSigma[] = "spatial_filter_sigma";
constexpr char kRangeFilterSigma[] = "range_filter_sigma";
constexpr char kIirFilterStrength[] = "iir_filter_strength";

constexpr char kDenoiserEnable[] = "denoiser_enable";
constexpr char kDenoiserIirTemporalConvergence[] =
    "denoiser_iir_temporal_convergence";
constexpr char kDenoiserNumSpatialPasses[] = "num_spatial_passes";
constexpr char kDenoiserSpatialStrength[] = "spatial_strength";

// Allocate one buffer for denoiser because we run the denoiser in IIR filter
// mode. We'll need to have more buffers if we run the burst denoising mode.
constexpr int kMaxDenoiserBurstLength = 1;

}  // namespace

//
// HdrNetStreamManipulator::HdrNetStreamContext implementations.
//

base::Optional<int> HdrNetStreamManipulator::HdrNetStreamContext::PopBuffer() {
  if (usable_buffer_list.empty()) {
    LOGF(ERROR) << "Buffer underrun";
    return base::nullopt;
  }
  HdrNetStreamContext::UsableBufferInfo buffer_info =
      std::move(usable_buffer_list.front());
  usable_buffer_list.pop();
  if (buffer_info.acquire_fence.is_valid() &&
      sync_wait(buffer_info.acquire_fence.get(), kDefaultSyncWaitTimeoutMs) !=
          0) {
    LOGF(WARNING) << "sync_wait timeout on acquiring usable HDRnet buffer";
    NOTREACHED();
  }
  return buffer_info.index;
}

void HdrNetStreamManipulator::HdrNetStreamContext::PushBuffer(
    int index, base::ScopedFD acquire_fence) {
  usable_buffer_list.push(
      {.index = index, .acquire_fence = std::move(acquire_fence)});
}

//
// HdrNetStreamManipulator::HdrNetRequestBufferInfo implementations.
//

HdrNetStreamManipulator::HdrNetRequestBufferInfo::HdrNetRequestBufferInfo(
    HdrNetStreamManipulator::HdrNetStreamContext* context,
    std::vector<camera3_stream_buffer_t>&& buffers)
    : stream_context(context),
      client_requested_yuv_buffers(std::move(buffers)) {}

HdrNetStreamManipulator::HdrNetRequestBufferInfo::HdrNetRequestBufferInfo(
    HdrNetStreamManipulator::HdrNetRequestBufferInfo&& other) {
  *this = std::move(other);
}

HdrNetStreamManipulator::HdrNetRequestBufferInfo&
HdrNetStreamManipulator::HdrNetRequestBufferInfo::operator=(
    HdrNetStreamManipulator::HdrNetRequestBufferInfo&& other) {
  if (this != &other) {
    Invalidate();
    stream_context = other.stream_context;
    buffer_index = other.buffer_index;
    other.buffer_index = kInvalidBufferIndex;
    release_fence = std::move(other.release_fence);
    client_requested_yuv_buffers.swap(other.client_requested_yuv_buffers);
    blob_result_pending = other.blob_result_pending;
    blob_intermediate_yuv_pending = other.blob_intermediate_yuv_pending;
    skip_hdrnet_processing = other.skip_hdrnet_processing;
    other.Invalidate();
  }
  return *this;
}

HdrNetStreamManipulator::HdrNetRequestBufferInfo::~HdrNetRequestBufferInfo() {
  Invalidate();
}

void HdrNetStreamManipulator::HdrNetRequestBufferInfo::Invalidate() {
  if (stream_context && buffer_index != kInvalidBufferIndex) {
    stream_context->PushBuffer(buffer_index, std::move(release_fence));
  }
  stream_context = nullptr;
  buffer_index = kInvalidBufferIndex;
  release_fence.reset();
  client_requested_yuv_buffers.clear();
  blob_result_pending = false;
  blob_intermediate_yuv_pending = false;
  skip_hdrnet_processing = false;
}

//
// HdrNetStreamManipulator implementations.
//

HdrNetStreamManipulator::HdrNetStreamManipulator(
    base::FilePath config_file_path,
    std::unique_ptr<StillCaptureProcessor> still_capture_processor,
    HdrNetProcessor::Factory hdrnet_processor_factory)
    : gpu_thread_("HdrNetPipelineGpuThread"),
      hdrnet_processor_factory_(
          !hdrnet_processor_factory.is_null()
              ? std::move(hdrnet_processor_factory)
              : base::BindRepeating(HdrNetProcessorImpl::CreateInstance)),
      config_(config_file_path,
              base::FilePath(HdrNetConfig::kOverrideHdrNetConfigFile)),
      still_capture_processor_(std::move(still_capture_processor)),
      camera_metrics_(CameraMetrics::New()),
      metadata_logger_({.dump_path = base::FilePath(kMetadataDumpPath)}) {
  CHECK(gpu_thread_.Start());
  config_.SetCallback(base::BindRepeating(
      &HdrNetStreamManipulator::OnOptionsUpdated, base::Unretained(this)));
}

HdrNetStreamManipulator::~HdrNetStreamManipulator() {
  gpu_thread_.PostTaskAsync(
      FROM_HERE, base::BindOnce(&HdrNetStreamManipulator::ResetStateOnGpuThread,
                                base::Unretained(this)));
  gpu_thread_.Stop();
}

bool HdrNetStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    CaptureResultCallback result_callback) {
  bool ret;
  gpu_thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&HdrNetStreamManipulator::InitializeOnGpuThread,
                     base::Unretained(this), base::Unretained(static_info),
                     std::move(result_callback)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  bool ret;
  gpu_thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&HdrNetStreamManipulator::ConfigureStreamsOnGpuThread,
                     base::Unretained(this), base::Unretained(stream_config)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  bool ret;
  gpu_thread_.PostTaskSync(
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
  bool ret;
  gpu_thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&HdrNetStreamManipulator::ProcessCaptureRequestOnGpuThread,
                     base::Unretained(this), base::Unretained(request)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor* result) {
  bool ret;
  gpu_thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&HdrNetStreamManipulator::ProcessCaptureResultOnGpuThread,
                     base::Unretained(this), base::Unretained(result)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::Notify(camera3_notify_msg_t* msg) {
  bool ret;
  gpu_thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&HdrNetStreamManipulator::NotifyOnGpuThread,
                     base::Unretained(this), base::Unretained(msg)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::Flush() {
  bool ret;
  gpu_thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&HdrNetStreamManipulator::FlushOnGpuThread,
                     base::Unretained(this)),
      &ret);
  return ret;
}

// static
HdrNetStreamManipulator::HdrNetBufferInfoList::iterator
HdrNetStreamManipulator::FindMatchingBufferInfo(
    HdrNetStreamManipulator::HdrNetBufferInfoList* list,
    const HdrNetStreamManipulator::HdrNetStreamContext* const context) {
  auto it = std::find_if(list->begin(), list->end(),
                         [context](const HdrNetRequestBufferInfo& buf_info) {
                           return buf_info.stream_context == context;
                         });
  return it;
}

HdrNetStreamManipulator::HdrNetRequestBufferInfo*
HdrNetStreamManipulator::GetBufferInfoWithPendingBlobStream(
    int frame_number, const camera3_stream_t* blob_stream) {
  auto iter = request_buffer_info_.find(frame_number);
  if (iter == request_buffer_info_.end()) {
    return nullptr;
  }
  for (auto& entry : iter->second) {
    if (entry.blob_result_pending &&
        entry.stream_context->original_stream == blob_stream) {
      return &entry;
    }
  }
  return nullptr;
}

bool HdrNetStreamManipulator::InitializeOnGpuThread(
    const camera_metadata_t* static_info,
    CaptureResultCallback result_callback) {
  DCHECK(gpu_thread_.IsCurrentThread());

  static_info_.acquire(clone_camera_metadata(static_info));
  result_callback_ = std::move(result_callback);
  return true;
}

bool HdrNetStreamManipulator::ConfigureStreamsOnGpuThread(
    Camera3StreamConfiguration* stream_config) {
  DCHECK(gpu_thread_.IsCurrentThread());

  // Clear the stream configuration from the previous session.
  ResetStateOnGpuThread();

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "Before stream manipulation:";
    for (const auto* s : stream_config->GetStreams()) {
      VLOGF(1) << GetDebugString(s);
    }
  }

  base::span<camera3_stream_t* const> client_requested_streams =
      stream_config->GetStreams();
  std::vector<camera3_stream_t*> modified_streams;
  int num_yuv_streams = 0;
  int num_blob_streams = 0;
  for (size_t i = 0; i < client_requested_streams.size(); ++i) {
    camera3_stream_t* s = client_requested_streams[i];
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

      // TODO(jcliang): See if we need to use 10-bit YUV (i.e. with format
      // HAL_PIXEL_FORMAT_YCBCR_P010);
      HdrNetStreamContext* context =
          CreateHdrNetStreamContext(s, HAL_PIXEL_FORMAT_YCbCr_420_888);
      // TODO(jcliang): We may need to treat YUV stream with maximum resolution
      // specially and mark it here, since it's what we use in YUV reprocessing.
      switch (context->mode) {
        case HdrNetStreamContext::Mode::kReplaceYuv:
          modified_streams.push_back(context->hdrnet_stream.get());
          ++num_yuv_streams;
          hdrnet_metrics_.max_yuv_stream_size =
              std::max(static_cast<int>(context->hdrnet_stream->width *
                                        context->hdrnet_stream->height),
                       hdrnet_metrics_.max_yuv_stream_size);
          break;

        case HdrNetStreamContext::Mode::kAppendWithBlob:
          DCHECK_EQ(s->format, HAL_PIXEL_FORMAT_BLOB);
          still_capture_processor_->Initialize(s, result_callback_);
          modified_streams.push_back(s);
          modified_streams.push_back(context->hdrnet_stream.get());
          ++num_blob_streams;
          hdrnet_metrics_.max_blob_stream_size =
              std::max(static_cast<int>(context->hdrnet_stream->width *
                                        context->hdrnet_stream->height),
                       hdrnet_metrics_.max_blob_stream_size);
          break;
      }
    }
  }

  stream_config->SetStreams(modified_streams);

  hdrnet_metrics_.num_concurrent_hdrnet_streams = hdrnet_stream_context_.size();
  bool has_different_aspect_ratio = false;
  for (size_t i = 1; i < hdrnet_stream_context_.size(); ++i) {
    const auto* s1 = hdrnet_stream_context_[i - 1]->hdrnet_stream.get();
    const auto* s2 = hdrnet_stream_context_[i]->hdrnet_stream.get();
    if (s1->width * s2->height != s2->width * s1->height) {
      has_different_aspect_ratio = true;
      break;
    }
  }
  if (num_yuv_streams == 1) {
    if (num_blob_streams == 0) {
      hdrnet_metrics_.stream_config =
          HdrnetStreamConfiguration::kSingleYuvStream;
    } else {
      hdrnet_metrics_.stream_config =
          HdrnetStreamConfiguration::kSingleYuvStreamWithBlob;
    }
  } else if (num_yuv_streams > 1) {
    if (num_blob_streams == 0) {
      hdrnet_metrics_.stream_config =
          has_different_aspect_ratio
              ? HdrnetStreamConfiguration::
                    kMultipleYuvStreamsOfDifferentAspectRatio
              : HdrnetStreamConfiguration::kMultipleYuvStreams;
    } else {
      hdrnet_metrics_.stream_config =
          has_different_aspect_ratio
              ? HdrnetStreamConfiguration::
                    kMultipleYuvStreamsOfDifferentAspectRatioWithBlob
              : HdrnetStreamConfiguration::kMultipleYuvStreamsWithBlob;
    }
  }

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "After stream manipulation:";
    for (const auto* s : stream_config->GetStreams()) {
      VLOGF(1) << GetDebugString(s);
    }
  }

  return true;
}

bool HdrNetStreamManipulator::OnConfiguredStreamsOnGpuThread(
    Camera3StreamConfiguration* stream_config) {
  DCHECK(gpu_thread_.IsCurrentThread());

  // Restore HDRnet streams to the original streams.
  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "Before stream manipulation:";
    for (const auto* s : stream_config->GetStreams()) {
      VLOGF(1) << GetDebugString(s);
    }
  }

  base::span<camera3_stream_t* const> modified_streams =
      stream_config->GetStreams();
  std::vector<camera3_stream_t*> restored_streams;
  for (auto* modified_stream : modified_streams) {
    HdrNetStreamContext* context =
        GetHdrNetContextFromHdrNetStream(modified_stream);
    if (!context) {
      // Not a stream that we replaced, so pass to client directly.
      restored_streams.push_back(modified_stream);
      continue;
    }
    switch (context->mode) {
      case HdrNetStreamContext::Mode::kReplaceYuv: {
        // Propagate the fields set by HAL back to the client.
        camera3_stream_t* original_stream = context->original_stream;
        original_stream->max_buffers = modified_stream->max_buffers;
        original_stream->usage = modified_stream->usage;
        original_stream->priv = modified_stream->priv;
        restored_streams.push_back(original_stream);
        break;
      }

      case HdrNetStreamContext::Mode::kAppendWithBlob:
        // Skip the HDRnet stream we added for BLOB.
        break;
    }
  }

  stream_config->SetStreams(restored_streams);

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "After stream manipulation:";
    for (const auto* s : stream_config->GetStreams()) {
      VLOGF(1) << GetDebugString(s);
    }
  }

  bool success = SetUpPipelineOnGpuThread();
  if (!success) {
    LOGF(ERROR) << "Cannot set up HDRnet pipeline";
    return false;
  }

  return true;
}

bool HdrNetStreamManipulator::ProcessCaptureRequestOnGpuThread(
    Camera3CaptureDescriptor* request) {
  DCHECK(gpu_thread_.IsCurrentThread());

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, request->frame_number()) << " Got request:";
    if (request->GetInputBuffer()) {
      VLOGF(2) << "\t" << GetDebugString(request->GetInputBuffer()->stream);
    }
    for (const auto& request_buffer : request->GetOutputBuffers()) {
      VLOGF(2) << "\t" << GetDebugString(request_buffer.stream);
    }
  }

  bool skip_hdrnet_processing = false;
  base::span<const uint8_t> tm_mode =
      request->GetMetadata<uint8_t>(ANDROID_TONEMAP_MODE);
  if (!tm_mode.empty() && (tm_mode[0] == ANDROID_TONEMAP_MODE_CONTRAST_CURVE ||
                           tm_mode[0] == ANDROID_TONEMAP_MODE_GAMMA_VALUE ||
                           tm_mode[0] == ANDROID_TONEMAP_MODE_PRESET_CURVE)) {
    skip_hdrnet_processing = true;
  }

  if (request->GetInputBuffer() != nullptr) {
    // Skip reprocessing requests. We can't touch the output buffers of a
    // reprocessing request since they have to be produced from the given input
    // buffer.
    return true;
  }

  for (auto& context : hdrnet_stream_context_) {
    context->processor->SetOptions(
        {.metadata_logger =
             options_.log_frame_metadata ? &metadata_logger_ : nullptr});
  }

  // First, pick the set of HDRnet stream that we will put into the request.
  base::span<const camera3_stream_buffer_t> client_output_buffers =
      request->GetOutputBuffers();
  std::vector<camera3_stream_buffer_t> modified_output_buffers;
  HdrNetBufferInfoList hdrnet_buf_to_add;
  for (const auto& request_buffer : client_output_buffers) {
    HdrNetStreamContext* stream_context =
        GetHdrNetContextFromRequestedStream(request_buffer.stream);
    if (!stream_context) {
      // Not a stream that we care, so simply pass through to HAL.
      modified_output_buffers.push_back(request_buffer);
      continue;
    }

    // Only change the metadata when the client request settings is not null.
    // This is mainly to make the CTS tests happy, as some test cases set null
    // settings and if we change that the vendor camera HAL may not handle the
    // incremental changes well.
    if (request->has_metadata()) {
      stream_context->processor->WriteRequestParameters(request);
    }
    switch (stream_context->mode) {
      case HdrNetStreamContext::Mode::kReplaceYuv: {
        auto is_compatible =
            [stream_context](const HdrNetRequestBufferInfo& buf_info) {
              return (buf_info.stream_context->mode ==
                      HdrNetStreamContext::Mode::kReplaceYuv) &&
                     HaveSameAspectRatio(
                         buf_info.stream_context->hdrnet_stream.get(),
                         stream_context->hdrnet_stream.get());
            };
        auto it = std::find_if(hdrnet_buf_to_add.begin(),
                               hdrnet_buf_to_add.end(), is_compatible);
        if (it != hdrnet_buf_to_add.end()) {
          // Request only one stream and produce the other smaller buffers
          // through downscaling. This is more efficient than running HDRnet
          // processor for each buffer.
          if (stream_context->hdrnet_stream->width >
              it->stream_context->hdrnet_stream->width) {
            it->stream_context = stream_context;
          }
          it->client_requested_yuv_buffers.push_back(request_buffer);
        } else {
          HdrNetRequestBufferInfo buf_info(stream_context, {request_buffer});
          buf_info.skip_hdrnet_processing = skip_hdrnet_processing;
          hdrnet_buf_to_add.emplace_back(std::move(buf_info));
        }
        break;
      }

      case HdrNetStreamContext::Mode::kAppendWithBlob: {
        DCHECK_EQ(request_buffer.stream->format, HAL_PIXEL_FORMAT_BLOB);
        // Defer the final BLOB buffer to the StillCaptureProcessor as we'll be
        // handling the BLOB metadata and YUV buffer asynchronously.
        camera3_capture_request_t* locked_request = request->LockForRequest();
        still_capture_processor_->QueuePendingOutputBuffer(
            request->frame_number(), request_buffer, locked_request->settings);
        request->Unlock();
        // Still queue the BLOB buffer so that we can extract the metadata.
        modified_output_buffers.push_back(request_buffer);
        // Finally queue the HDRnet YUV buffer that will be used to produce the
        // BLOB image.
        HdrNetRequestBufferInfo buf_info(stream_context, {});
        buf_info.blob_result_pending = true;
        buf_info.blob_intermediate_yuv_pending = true;
        buf_info.skip_hdrnet_processing = skip_hdrnet_processing;
        hdrnet_buf_to_add.emplace_back(std::move(buf_info));
        break;
      }
    }
  }

  // After we have the set of HdrNet streams, allocate the HdrNet buffers for
  // the request.
  for (auto& info : hdrnet_buf_to_add) {
    base::Optional<int> buffer_index = info.stream_context->PopBuffer();
    if (!buffer_index) {
      // TODO(jcliang): This is unlikely, but we should report a buffer error in
      // this case.
      return false;
    }
    info.buffer_index = *buffer_index;
    modified_output_buffers.push_back(camera3_stream_buffer_t{
        .stream = info.stream_context->hdrnet_stream.get(),
        .buffer = const_cast<buffer_handle_t*>(
            &info.stream_context->shared_images[*buffer_index].buffer()),
        .status = CAMERA3_BUFFER_STATUS_OK,
        .acquire_fence = -1,
        .release_fence = -1,
    });
  }

  uint32_t frame_number = request->frame_number();
  request_buffer_info_.insert({frame_number, std::move(hdrnet_buf_to_add)});
  request->SetOutputBuffers(modified_output_buffers);

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, frame_number) << "Modified request:";
    base::span<const camera3_stream_buffer_t> output_buffers =
        request->GetOutputBuffers();
    for (const auto& request_buffer : output_buffers) {
      VLOGF(2) << "\t" << GetDebugString(request_buffer.stream);
    }
  }

  return true;
}

bool HdrNetStreamManipulator::ProcessCaptureResultOnGpuThread(
    Camera3CaptureDescriptor* result) {
  DCHECK(gpu_thread_.IsCurrentThread());

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, result->frame_number()) << "Got result:";
    if (result->GetInputBuffer()) {
      VLOGF(2) << "\t" << GetDebugString(result->GetInputBuffer()->stream);
    }
    for (const auto& hal_result_buffer : result->GetOutputBuffers()) {
      VLOGF(2) << "\t" << GetDebugString(hal_result_buffer.stream);
    }
  }

  if (result->has_metadata()) {
    if (options_.hdrnet_enable) {
      // Result metadata may come before the buffers due to partial results.
      for (const auto& context : hdrnet_stream_context_) {
        // TODO(jcliang): Update the LUT textures once and share it with all
        // processors.
        context->processor->ProcessResultMetadata(result);
      }
    }
  }

  if (result->num_output_buffers() == 0) {
    return true;
  }

  std::vector<camera3_stream_buffer_t> hdrnet_buffer_to_process;
  std::vector<camera3_stream_buffer_t> output_buffers_to_client;
  ExtractHdrNetBuffersToProcess(
      result->frame_number(), result->GetOutputBuffers(),
      &hdrnet_buffer_to_process, &output_buffers_to_client);

  auto clean_up = [&]() {
    // Send back the buffers with our buffer set.
    result->SetOutputBuffers(output_buffers_to_client);
    // Remove a pending request if the YUV buffers are done rendering and the
    // pending BLOB buffer is received.
    HdrNetBufferInfoList& pending_request_buffers =
        request_buffer_info_[result->frame_number()];
    for (auto it = pending_request_buffers.begin();
         it != pending_request_buffers.end();) {
      if (it->client_requested_yuv_buffers.empty() &&
          !it->blob_result_pending && !it->blob_intermediate_yuv_pending) {
        it = pending_request_buffers.erase(it);
      } else {
        ++it;
      }
    }
    if (pending_request_buffers.empty()) {
      VLOGFID(2, result->frame_number())
          << "Done processing all pending buffers";
      request_buffer_info_.erase(result->frame_number());
    }

    if (VLOG_IS_ON(2)) {
      VLOGFID(2, result->frame_number()) << "Modified result:";
      base::span<const camera3_stream_buffer_t> output_buffers =
          result->GetOutputBuffers();
      for (const auto& buffer : output_buffers) {
        VLOGF(2) << "\t" << GetDebugString(buffer.stream);
      }
    }
  };

  if (hdrnet_buffer_to_process.empty()) {
    clean_up();
    return true;
  }

  HdrNetBufferInfoList& pending_request_buffers =
      request_buffer_info_[result->frame_number()];

  // Process each HDRnet buffer in this capture result and produce the client
  // requested output buffers associated with each HDRnet buffer.
  for (auto& hdrnet_buffer : hdrnet_buffer_to_process) {
    HdrNetStreamContext* stream_context =
        GetHdrNetContextFromHdrNetStream(hdrnet_buffer.stream);
    auto request_buffer_info =
        FindMatchingBufferInfo(&pending_request_buffers, stream_context);
    DCHECK(request_buffer_info != pending_request_buffers.end());

    if (options_.denoiser_enable) {
      // Run the denoiser.
      SharedImage& input_img =
          stream_context->shared_images[request_buffer_info->buffer_index];
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

    std::vector<buffer_handle_t> buffers_to_render;
    if (!GetBuffersToRender(stream_context, &(*request_buffer_info),
                            &buffers_to_render)) {
      return false;
    }

    // Run the HDRNet pipeline and write to the buffers.
    HdrNetConfig::Options processor_config =
        PrepareProcessorConfig(result, *request_buffer_info);
    const SharedImage& image =
        options_.denoiser_enable
            ? stream_context->denoiser_intermediate
            : stream_context->shared_images[request_buffer_info->buffer_index];
    request_buffer_info->release_fence = stream_context->processor->Run(
        result->frame_number(), processor_config, image,
        base::ScopedFD(hdrnet_buffer.release_fence), buffers_to_render,
        &hdrnet_metrics_);

    OnBuffersRendered(result->frame_number(), stream_context,
                      &(*request_buffer_info), &output_buffers_to_client);
  }

  clean_up();
  return true;
}

bool HdrNetStreamManipulator::NotifyOnGpuThread(camera3_notify_msg_t* msg) {
  DCHECK(gpu_thread_.IsCurrentThread());
  // Free up buffers in case of error.

  if (msg->type == CAMERA3_MSG_ERROR) {
    camera3_error_msg_t& error = msg->message.error;
    VLOGFID(1, error.frame_number) << "Got error notify:"
                                   << " stream=" << error.error_stream
                                   << " errorcode=" << error.error_code;
    HdrNetStreamContext* stream_context =
        GetHdrNetContextFromHdrNetStream(error.error_stream);
    switch (error.error_code) {
      case CAMERA3_MSG_ERROR_DEVICE:
        // Nothing we can do here. Simply restore the stream and forward the
        // error.
      case CAMERA3_MSG_ERROR_RESULT:
        // Result metadata may not be available. We can still produce the
        // processed frame using metadata from previous frame.
        break;

      case CAMERA3_MSG_ERROR_REQUEST:
        // There will be no capture result, so simply destroy the associated
        // RequestContext to free the HdrNet buffers.
        if (request_buffer_info_.count(error.frame_number) == 0) {
          break;
        }
        request_buffer_info_.erase(error.frame_number);
        break;

      case CAMERA3_MSG_ERROR_BUFFER: {
        // The result buffer will not be available, so recycle the hdrnet
        // buffer.
        if (request_buffer_info_.count(error.frame_number) == 0) {
          break;
        }
        HdrNetBufferInfoList& buf_info =
            request_buffer_info_[error.frame_number];
        auto it = FindMatchingBufferInfo(&buf_info, stream_context);
        if (it != buf_info.end()) {
          buf_info.erase(it);
        }
        if (buf_info.empty()) {
          request_buffer_info_.erase(error.frame_number);
        }
        break;
      }
    }

    // Restore the original stream so the message makes sense to the client.
    if (stream_context) {
      error.error_stream = stream_context->original_stream;
    }

    ++hdrnet_metrics_.errors[HdrnetError::kCameraHal3Error];
  }

  return true;
}

bool HdrNetStreamManipulator::FlushOnGpuThread() {
  DCHECK(gpu_thread_.IsCurrentThread());

  return true;
}

void HdrNetStreamManipulator::ExtractHdrNetBuffersToProcess(
    int frame_number,
    base::span<const camera3_stream_buffer_t> raw_result_buffers,
    std::vector<camera3_stream_buffer_t>* hdrnet_buffer_to_process,
    std::vector<camera3_stream_buffer_t>* output_buffers_to_client) {
  DCHECK(hdrnet_buffer_to_process);
  DCHECK(output_buffers_to_client);
  for (const auto& hal_result_buffer : raw_result_buffers) {
    HdrNetStreamContext* hdrnet_stream_context =
        GetHdrNetContextFromHdrNetStream(hal_result_buffer.stream);
    if (hdrnet_stream_context) {
      hdrnet_buffer_to_process->push_back(hal_result_buffer);
      continue;
    }

    // The buffer is not a HDRnet buffer we added, but it may be a BLOB
    // buffer that a kAppendWithBlob HDRnet stream is associated with.
    if (hal_result_buffer.stream->format == HAL_PIXEL_FORMAT_BLOB) {
      HdrNetStreamContext* associated_stream_context =
          GetHdrNetContextFromRequestedStream(hal_result_buffer.stream);
      HdrNetRequestBufferInfo* request_info =
          GetBufferInfoWithPendingBlobStream(frame_number,
                                             hal_result_buffer.stream);
      if (associated_stream_context && request_info) {
        DCHECK_EQ(associated_stream_context->mode,
                  HdrNetStreamContext::Mode::kAppendWithBlob);
        still_capture_processor_->QueuePendingAppsSegments(
            frame_number, *hal_result_buffer.buffer);
        request_info->blob_result_pending = false;
        continue;
      }
    }

    // Not a buffer that we added or depend on, so pass to the client
    // directly.
    output_buffers_to_client->push_back(hal_result_buffer);
  }
}

bool HdrNetStreamManipulator::GetBuffersToRender(
    HdrNetStreamContext* stream_context,
    HdrNetRequestBufferInfo* request_buffer_info,
    std::vector<buffer_handle_t>* buffers_to_write) {
  DCHECK(stream_context);
  DCHECK(request_buffer_info);
  DCHECK(buffers_to_write);
  switch (stream_context->mode) {
    case HdrNetStreamContext::Mode::kReplaceYuv:
      // For normal YUV buffers: HDRnet pipeline writes to the client output
      // buffers directly. All the buffers in |request_buffer_info| having the
      // same aspect ratio as |stream_context| can be rendered in the same
      // batch.
      for (auto& requested_buffer :
           request_buffer_info->client_requested_yuv_buffers) {
        if (!HaveSameAspectRatio(stream_context->hdrnet_stream.get(),
                                 requested_buffer.stream)) {
          continue;
        }
        if (requested_buffer.acquire_fence != -1) {
          if (sync_wait(requested_buffer.acquire_fence,
                        kDefaultSyncWaitTimeoutMs) != 0) {
            LOGF(WARNING) << "sync_wait timeout on acquiring requested buffer";
            // TODO(jcliang): We should trigger a notify message of
            // buffer error here.
            ++hdrnet_metrics_.errors[HdrnetError::kSyncWaitError];
            return false;
          }
          close(requested_buffer.acquire_fence);
          requested_buffer.acquire_fence = -1;
        }
        buffers_to_write->push_back(*requested_buffer.buffer);
      }
      hdrnet_metrics_.max_output_buffers_rendered =
          std::max(static_cast<int>(buffers_to_write->size()),
                   hdrnet_metrics_.max_output_buffers_rendered);
      break;

    case HdrNetStreamContext::Mode::kAppendWithBlob:
      // For BLOB buffers: HDRnet writes to the intermediate buffer,
      // which will then be encoded into the JPEG image client
      // requested.
      buffers_to_write->push_back(*stream_context->still_capture_intermediate);
      ++hdrnet_metrics_.num_still_shot_taken;
      break;
  }
  return true;
}

HdrNetConfig::Options HdrNetStreamManipulator::PrepareProcessorConfig(
    Camera3CaptureDescriptor* result,
    const HdrNetRequestBufferInfo& buf_info) const {
  // Run the HDRNet pipeline and write to the buffers.
  HdrNetConfig::Options run_options = options_;

  // Use the HDR ratio calculated by Gcam AE if available.
  base::Optional<float> gcam_ae_hdr_ratio =
      result->feature_metadata().hdr_ratio;
  if (gcam_ae_hdr_ratio) {
    run_options.hdr_ratio = *result->feature_metadata().hdr_ratio;
    DVLOGFID(1, result->frame_number())
        << "Using HDR ratio=" << run_options.hdr_ratio;
  }

  // Disable HDRnet processing completely if the tonemap mode is set to contrast
  // curve, gamma value, or preset curve.
  if (buf_info.skip_hdrnet_processing) {
    run_options.hdrnet_enable = false;
    DVLOGFID(1, result->frame_number()) << "Disable HDRnet processing";
  }

  return run_options;
}

void HdrNetStreamManipulator::OnBuffersRendered(
    int frame_number,
    HdrNetStreamContext* stream_context,
    HdrNetRequestBufferInfo* request_buffer_info,
    std::vector<camera3_stream_buffer_t>* output_buffers_to_client) {
  DCHECK(stream_context);
  DCHECK(request_buffer_info);
  DCHECK(output_buffers_to_client);
  switch (stream_context->mode) {
    case HdrNetStreamContext::Mode::kReplaceYuv:
      // Assign the release fence to all client-requested buffers the
      // HDRnet pipeline writes to. The FD ownership will be passed to
      // the client.
      for (auto& requested_buffer :
           request_buffer_info->client_requested_yuv_buffers) {
        if (!HaveSameAspectRatio(stream_context->hdrnet_stream.get(),
                                 requested_buffer.stream)) {
          continue;
        }
        requested_buffer.release_fence =
            DupWithCloExec(request_buffer_info->release_fence.get()).release();
        output_buffers_to_client->push_back(requested_buffer);
      }
      request_buffer_info->client_requested_yuv_buffers.clear();
      break;

    case HdrNetStreamContext::Mode::kAppendWithBlob:
      // The JPEG result buffer will be produced by
      // |still_capture_processor_| asynchronously.
      still_capture_processor_->QueuePendingYuvImage(
          frame_number, *stream_context->still_capture_intermediate);
      request_buffer_info->blob_intermediate_yuv_pending = false;
      break;
  }
}

bool HdrNetStreamManipulator::SetUpPipelineOnGpuThread() {
  DCHECK(gpu_thread_.IsCurrentThread());

  if (!egl_context_) {
    egl_context_ = EglContext::GetSurfacelessContext();
    if (!egl_context_->IsValid()) {
      LOGF(ERROR) << "Failed to create EGL context";
      ++hdrnet_metrics_.errors[HdrnetError::kInitializationError];
      return false;
    }
  }
  if (!egl_context_->MakeCurrent()) {
    LOGF(ERROR) << "Failed to make EGL context current";
    ++hdrnet_metrics_.errors[HdrnetError::kInitializationError];
    return false;
  }

  std::vector<Size> all_output_sizes;
  for (const auto& context : hdrnet_stream_context_) {
    all_output_sizes.push_back(
        Size(context->hdrnet_stream->width, context->hdrnet_stream->height));
  }

  const camera_metadata_t* locked_static_info = static_info_.getAndLock();
  for (const auto& context : hdrnet_stream_context_) {
    camera3_stream_t* stream = context->hdrnet_stream.get();
    Size stream_size(stream->width, stream->height);
    std::vector<Size> viable_output_sizes;
    for (const auto& s : all_output_sizes) {
      if (s.width <= stream_size.width && s.height <= stream_size.height) {
        viable_output_sizes.push_back(s);
      }
    }
    context->processor = hdrnet_processor_factory_.Run(
        locked_static_info, gpu_thread_.task_runner());
    context->denoiser = SpatiotemporalDenoiser::CreateInstance(
        {.frame_width = static_cast<int>(stream_size.width),
         .frame_height = static_cast<int>(stream_size.height),
         .mode = SpatiotemporalDenoiser::Mode::kIirMode});
    if (!context->denoiser) {
      LOGF(ERROR) << "Failed to initialize Spatiotemporal denoiser";
      ++hdrnet_metrics_.errors[HdrnetError::kInitializationError];
      return false;
    }
    context->processor->Initialize(stream_size, viable_output_sizes);
    if (!context->processor) {
      LOGF(ERROR) << "Failed to initialize HDRnet processor";
      ++hdrnet_metrics_.errors[HdrnetError::kInitializationError];
      return false;
    }

    constexpr uint32_t kBufferUsage =
        GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_TEXTURE;
    // Allocate the hdrnet buffers.
    constexpr int kNumExtraBuffer = kMaxDenoiserBurstLength + 5;
    for (int i = 0; i < stream->max_buffers + kNumExtraBuffer; ++i) {
      ScopedBufferHandle buffer = CameraBufferManager::AllocateScopedBuffer(
          stream->width, stream->height, stream->format, kBufferUsage);
      if (!buffer) {
        LOGF(ERROR) << "Cannot allocate HDRnet buffers";
        ++hdrnet_metrics_.errors[HdrnetError::kInitializationError];
        return false;
      }
      SharedImage shared_image = SharedImage::CreateFromBuffer(
          *buffer, Texture2D::Target::kTarget2D, true);
      if (!shared_image.y_texture().IsValid() ||
          !shared_image.uv_texture().IsValid()) {
        LOGF(ERROR) << "Cannot create SharedImage for the HDRnet buffer";
        ++hdrnet_metrics_.errors[HdrnetError::kInitializationError];
        return false;
      }
      // Let the SharedImage own the buffer.
      shared_image.SetDestructionCallback(
          base::BindOnce([](ScopedBufferHandle buffer) {}, std::move(buffer)));
      context->shared_images.emplace_back(std::move(shared_image));
      context->PushBuffer(i, base::ScopedFD());
    }

    if (context->original_stream->format == HAL_PIXEL_FORMAT_BLOB) {
      LOGF(INFO) << "Allocate still capture intermediate";
      context->still_capture_intermediate =
          CameraBufferManager::AllocateScopedBuffer(
              stream->width, stream->height, HAL_PIXEL_FORMAT_YCBCR_420_888,
              kBufferUsage);
    }

    {
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
  }
  static_info_.unlock(locked_static_info);

  return true;
}

void HdrNetStreamManipulator::ResetStateOnGpuThread() {
  DCHECK(gpu_thread_.IsCurrentThread());

  still_capture_processor_->Reset();
  request_buffer_info_.clear();
  hdrnet_stream_context_.clear();
  request_stream_mapping_.clear();
  result_stream_mapping_.clear();

  UploadMetrics();
  hdrnet_metrics_ = HdrnetMetrics();
}

HdrNetStreamManipulator::HdrNetStreamContext*
HdrNetStreamManipulator::CreateHdrNetStreamContext(camera3_stream_t* requested,
                                                   uint32_t replace_format) {
  std::unique_ptr<HdrNetStreamContext> context =
      std::make_unique<HdrNetStreamContext>();
  context->original_stream = requested;
  context->hdrnet_stream = std::make_unique<camera3_stream_t>(*requested);
  context->hdrnet_stream->format = replace_format;
  if (requested->format == HAL_PIXEL_FORMAT_BLOB) {
    // We still need the BLOB stream for extracting the JPEG APPs segments, so
    // we add a new YUV stream instead of replacing the BLOB stream.
    context->mode = HdrNetStreamContext::Mode::kAppendWithBlob;
  }

  HdrNetStreamContext* addr = context.get();
  request_stream_mapping_[requested] = addr;
  result_stream_mapping_[context->hdrnet_stream.get()] = addr;
  hdrnet_stream_context_.emplace_back(std::move(context));
  return addr;
}

HdrNetStreamManipulator::HdrNetStreamContext*
HdrNetStreamManipulator::GetHdrNetContextFromRequestedStream(
    camera3_stream_t* requested) {
  auto iter = request_stream_mapping_.find(requested);
  if (iter == request_stream_mapping_.end()) {
    return nullptr;
  }
  return iter->second;
}

HdrNetStreamManipulator::HdrNetStreamContext*
HdrNetStreamManipulator::GetHdrNetContextFromHdrNetStream(
    camera3_stream_t* hdrnet) {
  auto iter = result_stream_mapping_.find(hdrnet);
  if (iter == result_stream_mapping_.end()) {
    return nullptr;
  }
  return iter->second;
}

void HdrNetStreamManipulator::OnOptionsUpdated(const base::Value& json_values) {
  LoadIfExist(json_values, kHdrNetEnableKey, &options_.hdrnet_enable);
  LoadIfExist(json_values, kDumpBufferKey, &options_.dump_buffer);
  LoadIfExist(json_values, kHdrRatioKey, &options_.hdr_ratio);
  LoadIfExist(json_values, kMaxGainBlendThresholdKey,
              &options_.max_gain_blend_threshold);
  LoadIfExist(json_values, kSpatialFilterSigma, &options_.spatial_filter_sigma);
  LoadIfExist(json_values, kRangeFilterSigma, &options_.range_filter_sigma);
  LoadIfExist(json_values, kIirFilterStrength, &options_.iir_filter_strength);

  bool denoiser_enable;
  if (LoadIfExist(json_values, kDenoiserEnable, &denoiser_enable)) {
    if (!options_.denoiser_enable && options_.denoiser_enable) {
      // Reset the denoiser temporal buffer whenever we switch on the denoiser
      // to avoid artifacts caused by stale data.
      for (auto& c : hdrnet_stream_context_) {
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

  DCHECK_GE(options_.hdr_ratio, 1.0f);
  DCHECK_LE(options_.max_gain_blend_threshold, 1.0f);
  DCHECK_GE(options_.max_gain_blend_threshold, 0.0f);
  DCHECK_LE(options_.iir_filter_strength, 1.0f);
  DCHECK_GE(options_.iir_filter_strength, 0.0f);

  bool log_frame_metadata;
  if (LoadIfExist(json_values, kLogFrameMetadataKey, &log_frame_metadata)) {
    if (options_.log_frame_metadata && !log_frame_metadata) {
      // Dump frame metadata when metadata logging if turned off.
      metadata_logger_.DumpMetadata();
      metadata_logger_.Clear();
    }
    options_.log_frame_metadata = log_frame_metadata;
  }

  DVLOGF(1) << "HDRnet config:"
            << " hdrnet_enable=" << options_.hdrnet_enable
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
