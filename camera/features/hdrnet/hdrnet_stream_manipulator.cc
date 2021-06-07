/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/hdrnet_stream_manipulator.h"

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <sync/sync.h>
#include <system/camera_metadata.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/common.h"
#include "features/hdrnet/hdrnet_processor_impl.h"
#include "gpu/egl/egl_fence.h"
#include "gpu/egl/utils.h"
#include "gpu/gles/texture_2d.h"
#include "gpu/gles/utils.h"

namespace cros {

namespace {

constexpr int kDefaultSyncWaitTimeoutMs = 300;

// Utility function to produce a debug string for the given camera3_stream_t
// |stream|.
inline std::string GetDebugString(const camera3_stream_t* stream) {
  return base::StringPrintf(
      "stream=%p, type=%d, size=%ux%u, format=%d, usage=%u, max_buffers=%u",
      stream, stream->stream_type, stream->width, stream->height,
      stream->format, stream->usage, stream->max_buffers);
}

}  // namespace

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

HdrNetStreamManipulator::HdrNetStreamManipulator(
    HdrNetProcessor::Factory hdrnet_processor_factory)
    : gpu_thread_("HdrNetPipelineGpuThread"),
      hdrnet_processor_factory_(
          !hdrnet_processor_factory.is_null()
              ? std::move(hdrnet_processor_factory)
              : base::BindRepeating(HdrNetProcessorImpl::GetInstance)) {
  CHECK(gpu_thread_.Start());
}

HdrNetStreamManipulator::~HdrNetStreamManipulator() {
  gpu_thread_.Stop();
}

bool HdrNetStreamManipulator::Initialize(const camera_metadata_t* static_info) {
  bool ret;
  gpu_thread_.PostTaskSync(
      FROM_HERE,
      base::Bind(&HdrNetStreamManipulator::InitializeOnGpuThread,
                 base::Unretained(this), base::Unretained(static_info)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::ConfigureStreams(
    camera3_stream_configuration_t* stream_list,
    std::vector<camera3_stream_t*>* streams) {
  bool ret;
  gpu_thread_.PostTaskSync(
      FROM_HERE,
      base::Bind(&HdrNetStreamManipulator::ConfigureStreamsOnGpuThread,
                 base::Unretained(this), base::Unretained(stream_list),
                 base::Unretained(streams)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::OnConfiguredStreams(
    camera3_stream_configuration_t* stream_list) {
  bool ret;
  gpu_thread_.PostTaskSync(
      FROM_HERE,
      base::Bind(&HdrNetStreamManipulator::OnConfiguredStreamsOnGpuThread,
                 base::Unretained(this), base::Unretained(stream_list)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::ProcessCaptureRequest(
    camera3_capture_request_t* request) {
  bool ret;
  gpu_thread_.PostTaskSync(
      FROM_HERE,
      base::Bind(&HdrNetStreamManipulator::ProcessCaptureRequestOnGpuThread,
                 base::Unretained(this), base::Unretained(request)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::ProcessCaptureResult(
    camera3_capture_result_t* result) {
  bool ret;
  gpu_thread_.PostTaskSync(
      FROM_HERE,
      base::Bind(&HdrNetStreamManipulator::ProcessCaptureResultOnGpuThread,
                 base::Unretained(this), base::Unretained(result)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::Notify(camera3_notify_msg_t* msg) {
  bool ret;
  gpu_thread_.PostTaskSync(
      FROM_HERE,
      base::Bind(&HdrNetStreamManipulator::NotifyOnGpuThread,
                 base::Unretained(this), base::Unretained(msg)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::Flush() {
  bool ret;
  gpu_thread_.PostTaskSync(
      FROM_HERE,
      base::Bind(&HdrNetStreamManipulator::FlushOnGpuThread,
                 base::Unretained(this)),
      &ret);
  return ret;
}

bool HdrNetStreamManipulator::InitializeOnGpuThread(
    const camera_metadata_t* static_info) {
  DCHECK(gpu_thread_.IsCurrentThread());

  static_info_.acquire(clone_camera_metadata(static_info));
  return true;
}

bool HdrNetStreamManipulator::ConfigureStreamsOnGpuThread(
    camera3_stream_configuration_t* stream_list,
    std::vector<camera3_stream_t*>* streams) {
  DCHECK(gpu_thread_.IsCurrentThread());

  // Clear the stream configuration from the previous session.
  ResetStateOnGpuThread();

  VLOGF(1) << "Before stream manipulation:";
  for (int i = 0; i < stream_list->num_streams; ++i) {
    camera3_stream_t* s = stream_list->streams[i];
    VLOGF(1) << GetDebugString(s);
    if (s->stream_type != CAMERA3_STREAM_OUTPUT ||
        !(s->format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
          s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED)) {
      // Only YUV output buffers are supported.
      continue;
    }
    if (s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED &&
        (s->usage & GRALLOC_USAGE_HW_CAMERA_ZSL) ==
            GRALLOC_USAGE_HW_CAMERA_ZSL) {
      // Ignore ZSL streams.
      continue;
    }
    // TODO(jcliang): Enable all resolutions once the HAL is ready.
    if ((s->width == 1920 && s->height == 1080) ||
        (s->width == 1280 && s->height == 720)) {
      HdrNetStreamContext* context;
      // TODO(jcliang): See if we need to use 10-bit YUV (i.e. with format
      // HAL_PIXEL_FORMAT_YCBCR_P010);
      context = CreateReplaceContext(s, HAL_PIXEL_FORMAT_YCbCr_420_888);
      stream_list->streams[i] = context->hdrnet_stream.get();
    }
  }

  VLOGF(1) << "After stream manipulation:";
  for (int i = 0; i < stream_list->num_streams; ++i) {
    VLOGF(1) << GetDebugString(stream_list->streams[i]);
  }

  return true;
}

bool HdrNetStreamManipulator::OnConfiguredStreamsOnGpuThread(
    camera3_stream_configuration_t* stream_list) {
  DCHECK(gpu_thread_.IsCurrentThread());

  // Restore 1280x720 or 1920x1080 P010 stream to the original NV12 stream.
  VLOGF(1) << "Before stream manipulation:";
  for (int i = 0; i < stream_list->num_streams; ++i) {
    camera3_stream_t* s = stream_list->streams[i];
    VLOGF(1) << GetDebugString(s);
    if (s->stream_type == CAMERA3_STREAM_INPUT ||
        !(s->format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
          s->format == HAL_PIXEL_FORMAT_YCBCR_P010)) {
      continue;
    }
    // TODO(jcliang): Enable all resolutions once the HAL is ready.
    if ((s->width == 1920 && s->height == 1080) ||
        (s->width == 1280 && s->height == 720)) {
      // Sync the stream state from replace to original.
      HdrNetStreamContext* context = GetReplaceContextFromReplacement(s);
      if (!context) {
        LOGF(ERROR) << "Cannot find stream replacement context";
        return false;
      }
      camera3_stream_t* original_stream = context->original_stream;
      original_stream->max_buffers = s->max_buffers;
      original_stream->usage = s->usage;
      original_stream->priv = s->priv;
      stream_list->streams[i] = original_stream;
    }
  }

  bool success = SetUpPipelineOnGpuThread();
  if (!success) {
    LOGF(ERROR) << "Cannot set up HDRnet pipeline";
    return false;
  }

  VLOGF(1) << "After stream manipulation:";
  for (int i = 0; i < stream_list->num_streams; ++i) {
    VLOGF(1) << GetDebugString(stream_list->streams[i]);
  }

  return true;
}

bool HdrNetStreamManipulator::ProcessCaptureRequestOnGpuThread(
    camera3_capture_request_t* request) {
  DCHECK(gpu_thread_.IsCurrentThread());

  camera_metadata_t* request_metadata =
      const_cast<camera_metadata_t*>(request->settings);
  UpdateCaptureMetadataOnGpuThread(request_metadata);

  RequestContext request_context;
  HdrNetStreamContext* candidate = nullptr;

  VLOGF(2) << "[" << request->frame_number << "] Got request:";
  for (int i = 0; i < request->num_output_buffers; ++i) {
    const camera3_stream_buffer_t* request_buffer = &request->output_buffers[i];
    VLOGF(2) << "\t" << GetDebugString(request_buffer->stream);

    HdrNetStreamContext* stream_context =
        GetReplaceContextFromOriginal(request_buffer->stream);
    if (!stream_context) {
      // Not a stream that we care, so simply pass through to HAL.
      request_context.modified_buffers.push_back(*request_buffer);
      continue;
    }
    // Record the client-requested buffers that we will produce with the HDRnet
    // processor.
    request_context.requested_buffers.push_back(*request_buffer);
    if (!candidate || (stream_context->hdrnet_stream->width >
                           candidate->hdrnet_stream->width &&
                       stream_context->hdrnet_stream->height >
                           candidate->hdrnet_stream->height)) {
      // Request only one stream and produce the other buffers through
      // downscaling. This is more efficient than running HDRnet processor for
      // each buffer.
      candidate = stream_context;
    }
  }

  if (candidate) {
    base::Optional<int> buffer_index = candidate->PopBuffer();
    if (!buffer_index) {
      return false;
    }
    request_context.buffer_index = *buffer_index;
    request_context.modified_buffers.push_back(camera3_stream_buffer_t{
        .stream = candidate->hdrnet_stream.get(),
        .buffer = const_cast<buffer_handle_t*>(
            &candidate->shared_images[*buffer_index].buffer()),
        .status = CAMERA3_BUFFER_STATUS_OK,
        .acquire_fence = -1,
        .release_fence = -1,
    });
  }

  uint32_t frame_number = request->frame_number;
  request_context_.insert({frame_number, std::move(request_context)});
  request->num_output_buffers =
      request_context_[frame_number].modified_buffers.size();
  request->output_buffers =
      request_context_[frame_number].modified_buffers.data();

  VLOGF(2) << "[" << request->frame_number << "] Modified request:";
  for (int i = 0; i < request->num_output_buffers; ++i) {
    camera3_stream_buffer_t* request_buffer =
        const_cast<camera3_stream_buffer_t*>(&request->output_buffers[i]);
    VLOGF(2) << "\t" << GetDebugString(request_buffer->stream);
  }

  return true;
}

bool HdrNetStreamManipulator::ProcessCaptureResultOnGpuThread(
    camera3_capture_result_t* result) {
  DCHECK(gpu_thread_.IsCurrentThread());

  HdrNetConfig::Options options = config_.GetOptions();

  if (result->result && options.enable) {
    // Result metadata may come before the buffers due to partial results.
    for (const auto& context : stream_replace_context_) {
      // TODO(jcliang): Update the LUT textures once and share it with all
      // processors.
      context->processor->ProcessResultMetadata(result->frame_number,
                                                result->result);
    }
  }

  if (result->num_output_buffers == 0) {
    return true;
  }

  // Run the HDRnet pipeline if there's a buffer that we care.
  HdrNetStreamContext* stream_context = nullptr;
  camera3_stream_buffer_t* hal_buffer_to_process = nullptr;
  VLOGF(2) << "[" << result->frame_number << "] Got result:";
  for (int i = 0; i < result->num_output_buffers; ++i) {
    camera3_stream_buffer_t* hal_result_buffer =
        const_cast<camera3_stream_buffer_t*>(&result->output_buffers[i]);
    VLOGF(2) << "\t" << GetDebugString(hal_result_buffer->stream);
    stream_context =
        GetReplaceContextFromReplacement(hal_result_buffer->stream);
    if (stream_context) {
      hal_buffer_to_process = hal_result_buffer;
      break;
    }
  }

  if (stream_context) {
    // Run the HDRNet pipeline and convert the buffers.
    HdrNetConfig::Options processor_config = config_.GetOptions();

    // Prepare the set of client-requested buffers that will written to by the
    // HDRnet pipeline.
    RequestContext& request_context = request_context_[result->frame_number];
    std::vector<buffer_handle_t> buffers_to_write;
    for (const auto& requested_buffer : request_context.requested_buffers) {
      if (requested_buffer.acquire_fence != -1) {
        if (sync_wait(requested_buffer.acquire_fence,
                      kDefaultSyncWaitTimeoutMs) != 0) {
          LOGF(WARNING) << "sync_wait timeout on acquiring requested buffer";
          // TODO(jcliang): We should trigger a notify message of buffer error
          // here.
          return false;
        }
        close(requested_buffer.acquire_fence);
      }
      buffers_to_write.push_back(*requested_buffer.buffer);
    }

    const SharedImage& image =
        stream_context->shared_images[request_context.buffer_index];
    base::ScopedFD hdrnet_release_fence = stream_context->processor->Run(
        result->frame_number, processor_config, image,
        base::ScopedFD(hal_buffer_to_process->release_fence), buffers_to_write);

    // Assign the release fence to all client-requested buffers the HDRnet
    // pipeline writes to.
    for (auto& requested_buffer : request_context.requested_buffers) {
      requested_buffer.release_fence =
          DupWithCloExec(hdrnet_release_fence.get()).release();
    }

    // Clean up the mapping and return the free replacement buffer.
    stream_context->PushBuffer(request_context.buffer_index,
                               DupWithCloExec(hdrnet_release_fence.get()));

    // Prepare the set of buffers that we'll send back to the client. Include
    // any buffer that's not replaced by us.
    for (int i = 0; i < result->num_output_buffers; ++i) {
      camera3_stream_buffer_t* hal_result_buffer =
          const_cast<camera3_stream_buffer_t*>(&result->output_buffers[i]);
      HdrNetStreamContext* stream_context =
          GetReplaceContextFromReplacement(hal_result_buffer->stream);
      if (!stream_context) {
        request_context.requested_buffers.push_back(*hal_result_buffer);
      }
    }
    // Send back the buffers with our buffer set.
    result->num_output_buffers = request_context.requested_buffers.size();
    result->output_buffers = request_context.requested_buffers.data();
  }

  // We don't delete the request context immediately because |requested_buffers|
  // needs to stay alive until the client finishes consuming it. Removing the
  // request contexts 6 frames after the result has been returned seems to be a
  // reasonable TTL. 6 is chosen because it's the common max_buffer setting on
  // Intel devices.
  constexpr uint32_t kRequestTTL = 6;
  if (result->frame_number >= kRequestTTL) {
    request_context_.erase(result->frame_number - kRequestTTL);
  }

  VLOGF(2) << "[" << result->frame_number << "] Modified result:";
  for (int i = 0; i < result->num_output_buffers; ++i) {
    VLOGF(2) << "\t" << GetDebugString(result->output_buffers[i].stream);
  }
  return true;
}

bool HdrNetStreamManipulator::NotifyOnGpuThread(camera3_notify_msg_t* msg) {
  DCHECK(gpu_thread_.IsCurrentThread());
  // Free up buffers in case of error.

  if (msg->type == CAMERA3_MSG_ERROR) {
    camera3_error_msg_t& error = msg->message.error;
    VLOGF(1) << "Got error notify: frame_number=" << error.frame_number
             << " stream=" << error.error_stream
             << " errorcode=" << error.error_code;
    HdrNetStreamContext* stream_context =
        GetReplaceContextFromReplacement(error.error_stream);
    switch (error.error_code) {
      case CAMERA3_MSG_ERROR_DEVICE:
        // Nothing we can do here. Simply restore the stream and forward the
        // error.
      case CAMERA3_MSG_ERROR_RESULT:
        // Result metadata may not be available. We can still produce the
        // processed frame using metadata from previous frame.
        break;

      case CAMERA3_MSG_ERROR_REQUEST:
      case CAMERA3_MSG_ERROR_BUFFER: {
        // There will be no capture result, or the result buffer will not be
        // available, so recycle the replacement buffer. The RequestContext in
        // |request_context_| will be erased in due time in
        // ProcessCaptureResultOnGpuThread().
        if (request_context_.count(error.frame_number) == 0) {
          break;
        }
        RequestContext& request_context = request_context_[error.frame_number];
        stream_context->PushBuffer(request_context.buffer_index,
                                   base::ScopedFD());
        break;
      }
    }

    // Restore the original stream so the message makes sense to the client.
    if (stream_context) {
      error.error_stream = stream_context->original_stream;
    }
  }

  return true;
}

bool HdrNetStreamManipulator::FlushOnGpuThread() {
  DCHECK(gpu_thread_.IsCurrentThread());

  return true;
}

bool HdrNetStreamManipulator::SetUpPipelineOnGpuThread() {
  DCHECK(gpu_thread_.IsCurrentThread());

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

  if (!image_processor_) {
    image_processor_ = std::make_unique<GpuImageProcessor>();
  }

  std::vector<Size> all_output_sizes;
  for (const auto& context : stream_replace_context_) {
    all_output_sizes.push_back(
        Size(context->hdrnet_stream->width, context->hdrnet_stream->height));
  }

  const camera_metadata_t* locked_static_info = static_info_.getAndLock();
  for (const auto& context : stream_replace_context_) {
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
    context->processor->Initialize(stream_size, viable_output_sizes);
    if (!context->processor) {
      return false;
    }

    constexpr uint32_t kBufferUsage =
        GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_TEXTURE;
    // Allocate the replacement buffers.
    constexpr int kNumExtraBuffer = 5;
    for (int i = 0; i < stream->max_buffers + kNumExtraBuffer; ++i) {
      ScopedBufferHandle buffer = CameraBufferManager::AllocateScopedBuffer(
          stream->width, stream->height, stream->format, kBufferUsage);
      if (!buffer) {
        LOGF(ERROR) << "Cannot allocate HDRnet buffers";
        return false;
      }
      SharedImage shared_image = SharedImage::CreateFromBuffer(
          *buffer, Texture2D::Target::kTarget2D, true);
      if (!shared_image.y_texture().IsValid() ||
          !shared_image.uv_texture().IsValid()) {
        LOGF(ERROR) << "Cannot create SharedImage for the HDRnet buffer";
        return false;
      }
      // Let the SharedImage own the buffer.
      shared_image.SetDestructionCallback(
          base::BindOnce([](ScopedBufferHandle buffer) {}, std::move(buffer)));
      context->shared_images.emplace_back(std::move(shared_image));
      context->PushBuffer(i, base::ScopedFD());
    }
  }
  static_info_.unlock(locked_static_info);

  return true;
}

void HdrNetStreamManipulator::ResetStateOnGpuThread() {
  DCHECK(gpu_thread_.IsCurrentThread());

  request_context_.clear();
  stream_replace_context_.clear();
  request_stream_mapping_.clear();
  result_stream_mapping_.clear();
}

void HdrNetStreamManipulator::UpdateCaptureMetadataOnGpuThread(
    camera_metadata_t* metadata) {
  DCHECK(gpu_thread_.IsCurrentThread());

  if (!egl_context_->MakeCurrent()) {
    LOGF(ERROR) << "Failed to make display current";
    return;
  }

  HdrNetConfig::Options options = config_.GetOptions();

  // The following metadata modifications are mainly for testing and debugging.
  // The change should only be triggered by changing the on-device config file
  // during testing and development, but not in production.
  //
  // TODO(jcliang): The AE compensation may be needed for production once we
  // integrate Gcam AE. We need to find a way to not set AE compensation on
  // production if we end up controlling AE in another way.
  base::Optional<int32_t*> exp_comp =
      GetMetadata<int32_t>(metadata, ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION);
  if (exp_comp) {
    **exp_comp = options.exp_comp;
  } else {
    LOGF(ERROR) << "Failed to update aeExposureCompensation";
  }
}

HdrNetStreamManipulator::HdrNetStreamContext*
HdrNetStreamManipulator::CreateReplaceContext(camera3_stream_t* original,
                                              uint32_t replace_format) {
  std::unique_ptr<HdrNetStreamContext> context =
      std::make_unique<HdrNetStreamContext>();
  context->original_stream = original;
  context->hdrnet_stream = std::make_unique<camera3_stream_t>(*original);
  context->hdrnet_stream->format = replace_format;

  HdrNetStreamContext* addr = context.get();
  request_stream_mapping_[original] = addr;
  result_stream_mapping_[context->hdrnet_stream.get()] = addr;
  stream_replace_context_.emplace_back(std::move(context));
  return addr;
}

HdrNetStreamManipulator::HdrNetStreamContext*
HdrNetStreamManipulator::GetReplaceContextFromOriginal(
    camera3_stream_t* original) {
  auto iter = request_stream_mapping_.find(original);
  if (iter == request_stream_mapping_.end()) {
    return nullptr;
  }
  return iter->second;
}

HdrNetStreamManipulator::HdrNetStreamContext*
HdrNetStreamManipulator::GetReplaceContextFromReplacement(
    camera3_stream_t* replace) {
  auto iter = result_stream_mapping_.find(replace);
  if (iter == result_stream_mapping_.end()) {
    return nullptr;
  }
  return iter->second;
}

}  // namespace cros
