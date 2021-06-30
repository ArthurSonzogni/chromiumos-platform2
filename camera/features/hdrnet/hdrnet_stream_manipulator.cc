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
#include "features/hdrnet/hdrnet_ae_controller_impl.h"
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

inline bool HaveSameAspectRatio(const camera3_stream_t* s1,
                                const camera3_stream_t* s2) {
  return (s1->width * s2->height == s1->height * s2->width);
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
    HdrNetProcessor::Factory hdrnet_processor_factory,
    HdrNetAeController::Factory hdrnet_ae_controller_factory)
    : gpu_thread_("HdrNetPipelineGpuThread"),
      hdrnet_processor_factory_(
          !hdrnet_processor_factory.is_null()
              ? std::move(hdrnet_processor_factory)
              : base::BindRepeating(HdrNetProcessorImpl::CreateInstance)),
      hdrnet_ae_controller_factory_(
          !hdrnet_ae_controller_factory.is_null()
              ? std::move(hdrnet_ae_controller_factory)
              : base::BindRepeating(HdrNetAeControllerImpl::CreateInstance)) {
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
  ae_controller_ = hdrnet_ae_controller_factory_.Run(static_info);
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
    HdrNetStreamContext* context;
    // TODO(jcliang): See if we need to use 10-bit YUV (i.e. with format
    // HAL_PIXEL_FORMAT_YCBCR_P010);
    context = CreateHdrNetStreamContext(s, HAL_PIXEL_FORMAT_YCbCr_420_888);
    stream_list->streams[i] = context->hdrnet_stream.get();
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

  // Restore HDRnet streams to the original NV12 streams.
  VLOGF(1) << "Before stream manipulation:";
  for (int i = 0; i < stream_list->num_streams; ++i) {
    camera3_stream_t* s = stream_list->streams[i];
    VLOGF(1) << GetDebugString(s);
    if (s->stream_type != CAMERA3_STREAM_OUTPUT ||
        !(s->format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
          s->format == HAL_PIXEL_FORMAT_YCBCR_P010)) {
      continue;
    }
    // Sync the stream state from replace to original.
    HdrNetStreamContext* context = GetHdrNetContextFromHdrNetStream(s);
    if (!context) {
      LOGF(ERROR) << "Cannot find HDRnet stream context";
      return false;
    }
    camera3_stream_t* original_stream = context->original_stream;
    original_stream->max_buffers = s->max_buffers;
    original_stream->usage = s->usage;
    original_stream->priv = s->priv;
    stream_list->streams[i] = original_stream;
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
  RecordCaptureMetadataOnGpuThread(request->frame_number, request_metadata);

  RequestContext request_context;
  std::vector<HdrNetStreamContext*> hdrnet_stream_contexts;

  VLOGF(2) << "[" << request->frame_number << "] Got request:";
  for (int i = 0; i < request->num_output_buffers; ++i) {
    const camera3_stream_buffer_t* request_buffer = &request->output_buffers[i];
    VLOGF(2) << "\t" << GetDebugString(request_buffer->stream);

    HdrNetStreamContext* stream_context =
        GetHdrNetContextFromRequestedStream(request_buffer->stream);
    if (!stream_context) {
      // Not a stream that we care, so simply pass through to HAL.
      request_context.modified_buffers.push_back(*request_buffer);
      continue;
    }
    // Record the client-requested buffers that we will produce with the HDRnet
    // processor.
    request_context.requested_buffers.push_back(*request_buffer);

    auto is_compatible = [stream_context](const HdrNetStreamContext* c) {
      return HaveSameAspectRatio(c->hdrnet_stream.get(),
                                 stream_context->hdrnet_stream.get());
    };
    auto it = std::find_if(hdrnet_stream_contexts.begin(),
                           hdrnet_stream_contexts.end(), is_compatible);
    if (it != hdrnet_stream_contexts.end()) {
      // Request only one stream and produce the other smaller buffers through
      // downscaling. This is more efficient than running HDRnet processor for
      // each buffer.
      if (stream_context->hdrnet_stream->width > (*it)->hdrnet_stream->width) {
        *it = stream_context;
      }
    } else {
      hdrnet_stream_contexts.push_back(stream_context);
    }
  }

  for (auto* c : hdrnet_stream_contexts) {
    base::Optional<int> buffer_index = c->PopBuffer();
    if (!buffer_index) {
      // TODO(jcliang): This is unlikely, but we should report a buffer error in
      // this case.
      return false;
    }
    request_context.buffer_index = *buffer_index;
    request_context.modified_buffers.push_back(camera3_stream_buffer_t{
        .stream = c->hdrnet_stream.get(),
        .buffer = const_cast<buffer_handle_t*>(
            &c->shared_images[*buffer_index].buffer()),
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

  if (result->result) {
    ae_controller_->RecordAeMetadata(result->frame_number, result->result);

    if (options.use_cros_face_detector) {
      // This is mainly for displaying the face rectangles in camera app for
      // development and debugging.
      ae_controller_->WriteResultFaceRectangles(
          const_cast<camera_metadata_t*>(result->result));
    }
    if (options.hdrnet_enable) {
      // Result metadata may come before the buffers due to partial results.
      for (const auto& context : stream_replace_context_) {
        // TODO(jcliang): Update the LUT textures once and share it with all
        // processors.
        context->processor->ProcessResultMetadata(result->frame_number,
                                                  result->result);
      }
    }
  }

  if (result->num_output_buffers == 0) {
    return true;
  }

  // Run the HDRnet pipeline if there's a buffer that we care.
  std::vector<std::pair<HdrNetStreamContext*, camera3_stream_buffer_t*>>
      stream_contexts;
  VLOGF(2) << "[" << result->frame_number << "] Got result:";
  for (int i = 0; i < result->num_output_buffers; ++i) {
    camera3_stream_buffer_t* hal_result_buffer =
        const_cast<camera3_stream_buffer_t*>(&result->output_buffers[i]);
    VLOGF(2) << "\t" << GetDebugString(hal_result_buffer->stream);
    HdrNetStreamContext* stream_context =
        GetHdrNetContextFromHdrNetStream(hal_result_buffer->stream);
    if (stream_context) {
      stream_contexts.emplace_back(stream_context, hal_result_buffer);
    }
  }

  if (!stream_contexts.empty()) {
    RequestContext& request_context = request_context_[result->frame_number];
    const SharedImage* yuv_image_to_record = nullptr;
    for (auto [stream_context, hal_buffer_to_process] : stream_contexts) {
      // Prepare the set of client-requested buffers that will written to by the
      // HDRnet pipeline.
      std::vector<camera3_stream_buffer_t*> stream_buffers_to_write;
      for (auto& requested_buffer : request_context.requested_buffers) {
        if (!HaveSameAspectRatio(requested_buffer.stream,
                                 stream_context->hdrnet_stream.get())) {
          continue;
        }
        if (requested_buffer.acquire_fence != -1) {
          if (sync_wait(requested_buffer.acquire_fence,
                        kDefaultSyncWaitTimeoutMs) != 0) {
            LOGF(WARNING) << "sync_wait timeout on acquiring requested buffer";
            // TODO(jcliang): We should trigger a notify message of buffer error
            // here.
            return false;
          }
          close(requested_buffer.acquire_fence);
          requested_buffer.acquire_fence = -1;
        }
        stream_buffers_to_write.push_back(&requested_buffer);
      }

      // Run the HDRNet pipeline and convert the buffers.
      HdrNetConfig::Options processor_config = config_.GetOptions();
      if (processor_config.gcam_ae_enable) {
        processor_config.hdr_ratio =
            ae_controller_->GetCalculatedHdrRatio(result->frame_number);
      }

      const SharedImage& image =
          stream_context->shared_images[request_context.buffer_index];
      std::vector<buffer_handle_t> buffers_to_write;
      for (auto* stream_buffer : stream_buffers_to_write) {
        buffers_to_write.push_back(*stream_buffer->buffer);
      }
      base::ScopedFD hdrnet_release_fence = stream_context->processor->Run(
          result->frame_number, processor_config, image,
          base::ScopedFD(hal_buffer_to_process->release_fence),
          buffers_to_write);

      // Assign the release fence to all client-requested buffers the HDRnet
      // pipeline writes to.
      for (auto* stream_buffer : stream_buffers_to_write) {
        stream_buffer->release_fence =
            DupWithCloExec(hdrnet_release_fence.get()).release();
      }

      // Clean up the mapping and return the free hdrnet buffer.
      stream_context->PushBuffer(request_context.buffer_index,
                                 DupWithCloExec(hdrnet_release_fence.get()));

      // Pass the buffer with the largest width to AE controller. This is a
      // heuristic and shouldn't matter for the majority of the time, as for
      // most cases the requested streams would have the same aspect ratio.
      if (!yuv_image_to_record ||
          (CameraBufferManager::GetWidth(image.buffer()) >
           CameraBufferManager::GetWidth(yuv_image_to_record->buffer()))) {
        yuv_image_to_record = &image;
      }
    }

    if (yuv_image_to_record) {
      RecordYuvBufferForAeControllerOnGpuThread(result->frame_number,
                                                *yuv_image_to_record);
    }

    // Prepare the set of buffers that we'll send back to the client. Include
    // any buffer that's not replaced by us.
    for (int i = 0; i < result->num_output_buffers; ++i) {
      camera3_stream_buffer_t* hal_result_buffer =
          const_cast<camera3_stream_buffer_t*>(&result->output_buffers[i]);
      HdrNetStreamContext* stream_context =
          GetHdrNetContextFromHdrNetStream(hal_result_buffer->stream);
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
      case CAMERA3_MSG_ERROR_BUFFER: {
        // There will be no capture result, or the result buffer will not be
        // available, so recycle the hdrnet buffer. The RequestContext in
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
      LOGF(ERROR) << "Failed to initialize HDRnet processor";
      return false;
    }

    constexpr uint32_t kBufferUsage =
        GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_HW_TEXTURE;
    // Allocate the hdrnet buffers.
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

void HdrNetStreamManipulator::RecordCaptureMetadataOnGpuThread(
    int frame_number, camera_metadata_t* metadata) {
  DCHECK(gpu_thread_.IsCurrentThread());

  if (!egl_context_->MakeCurrent()) {
    LOGF(ERROR) << "Failed to make display current";
    return;
  }

  HdrNetConfig::Options options = config_.GetOptions();
  HdrNetAeController::Options ae_controller_options = {
      .enabled = options.gcam_ae_enable,
      .ae_frame_interval = options.ae_frame_interval,
      .max_hdr_ratio = options.max_hdr_ratio,
      .use_cros_face_detector = options.use_cros_face_detector,
      .fd_frame_interval = options.fd_frame_interval,
      .ae_stats_input_mode = options.ae_stats_input_mode,
      .ae_override_mode = options.ae_override_mode,
      .log_frame_metadata = options.log_frame_metadata,
  };
  ae_controller_->SetOptions(ae_controller_options);

  ae_controller_->WriteRequestAeParameters(frame_number, metadata);
}

void HdrNetStreamManipulator::RecordYuvBufferForAeControllerOnGpuThread(
    int frame_number, const SharedImage& yuv_input) {
  DCHECK(gpu_thread_.IsCurrentThread());

  // TODO(jcliang): We may want to take the HDRnet-rendered buffer instead if
  // this is only used for face detection.
  ae_controller_->RecordYuvBuffer(frame_number, yuv_input.buffer(),
                                  base::ScopedFD());
}

HdrNetStreamManipulator::HdrNetStreamContext*
HdrNetStreamManipulator::CreateHdrNetStreamContext(camera3_stream_t* requested,
                                                   uint32_t replace_format) {
  std::unique_ptr<HdrNetStreamContext> context =
      std::make_unique<HdrNetStreamContext>();
  context->original_stream = requested;
  context->hdrnet_stream = std::make_unique<camera3_stream_t>(*requested);
  context->hdrnet_stream->format = replace_format;

  HdrNetStreamContext* addr = context.get();
  request_stream_mapping_[requested] = addr;
  result_stream_mapping_[context->hdrnet_stream.get()] = addr;
  stream_replace_context_.emplace_back(std::move(context));
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

}  // namespace cros
