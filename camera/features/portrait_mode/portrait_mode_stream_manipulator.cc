// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "features/portrait_mode/portrait_mode_stream_manipulator.h"

#include <cstdint>
#include <iterator>
#include <utility>

#include <base/task/bind_post_task.h>

#include "common/camera_buffer_handle.h"
#include "common/camera_hal3_helpers.h"
#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/common.h"
#include "features/portrait_mode/tracing.h"

namespace cros {

namespace {

bool UpdateResultMetadata(Camera3CaptureDescriptor* result,
                          SegmentationResult seg_result) {
  return result->UpdateMetadata<uint8_t>(
      kPortraitModeSegmentationResultVendorKey,
      std::array<uint8_t, 1>{base::checked_cast<unsigned char>(seg_result)});
}

}  // namespace

//
// PortraitModeStreamManipulator implementations.
//

PortraitModeStreamManipulator::PortraitModeStreamManipulator(
    CameraMojoChannelManagerToken* mojo_manager_token,
    std::unique_ptr<StillCaptureProcessor> still_capture_processor)
    : mojo_manager_token_(mojo_manager_token),
      still_capture_processor_(std::move(still_capture_processor)),
      thread_("PortraitModeThread") {
  CHECK(thread_.Start());
}

PortraitModeStreamManipulator::~PortraitModeStreamManipulator() {
  thread_.Stop();
}

// static
bool PortraitModeStreamManipulator::UpdateVendorTags(
    VendorTagManager& vendor_tag_manager) {
  if (!vendor_tag_manager.Add(kPortraitModeVendorKey,
                              kPortraitModeVendorTagSectionName,
                              kPortraitModeVendorTagName, TYPE_BYTE) ||
      !vendor_tag_manager.Add(kPortraitModeSegmentationResultVendorKey,
                              kPortraitModeVendorTagSectionName,
                              kPortraitModeResultVendorTagName, TYPE_BYTE)) {
    LOGF(ERROR) << "Failed to add the vendor tag for CrOS Portrait Mode";
    return false;
  }
  return true;
}

// static
bool PortraitModeStreamManipulator::UpdateStaticMetadata(
    android::CameraMetadata* static_info) {
  uint8_t update_portrait_vendor_key = 1;
  if (static_info->update(kPortraitModeVendorKey, &update_portrait_vendor_key,
                          1) != 0) {
    LOGF(ERROR) << "Failed to update kPortraitModeVendorKey to static metadata";
    return false;
  }
  return true;
}

bool PortraitModeStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  bool ret = false;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&PortraitModeStreamManipulator::InitializeOnThread,
                     base::Unretained(this), static_info, callbacks),
      &ret);
  return ret;
}

bool PortraitModeStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config,
    const StreamEffectMap* stream_effect_map) {
  bool ret = false;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&PortraitModeStreamManipulator::ConfigureStreamsOnThread,
                     base::Unretained(this), stream_config, stream_effect_map),
      &ret);
  return ret;
}

bool PortraitModeStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  bool ret = false;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(
          &PortraitModeStreamManipulator::OnConfiguredStreamsOnThread,
          base::Unretained(this), stream_config),
      &ret);
  return ret;
}

bool PortraitModeStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  return true;
}

bool PortraitModeStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  bool ret = false;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(
          &PortraitModeStreamManipulator::ProcessCaptureRequestOnThread,
          base::Unretained(this), request),
      &ret);
  return ret;
}

bool PortraitModeStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor result) {
  bool ret = false;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(
          &PortraitModeStreamManipulator::ProcessCaptureResultOnThread,
          base::Unretained(this), std::move(result)),
      &ret);
  return ret;
}

void PortraitModeStreamManipulator::Notify(camera3_notify_msg_t msg) {
  callbacks_.notify_callback.Run(std::move(msg));
}

bool PortraitModeStreamManipulator::Flush() {
  return true;
}

bool PortraitModeStreamManipulator::InitializeOnThread(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  CHECK(thread_.IsCurrentThread());
  TRACE_PORTRAIT_MODE();

  callbacks_ = std::move(callbacks);

  std::optional<int32_t> partial_result_count =
      GetRoMetadata<int32_t>(static_info, ANDROID_REQUEST_PARTIAL_RESULT_COUNT);
  partial_result_count_ = partial_result_count.value_or(1);
  VLOGF(1) << "Partial result count: " << partial_result_count_;

  // Initialize Portrait Mode effect.
  portrait_mode_ = std::make_unique<PortraitModeEffect>();
  if (portrait_mode_->Initialize(mojo_manager_token_) != 0) {
    LOGF(ERROR) << "Failed to initialize Portrait Mode effect";
    return false;
  }

  return true;
}

bool PortraitModeStreamManipulator::ConfigureStreamsOnThread(
    Camera3StreamConfiguration* stream_config,
    const StreamEffectMap* stream_effects_map) {
  CHECK(thread_.IsCurrentThread());
  TRACE_PORTRAIT_MODE();

  ResetOnThread();

  if (VLOG_IS_ON(2)) {
    VLOGF(2) << "Config streams from client:";
    for (auto* s : stream_config->GetStreams()) {
      VLOGF(2) << "  " << GetDebugString(s);
    }
  }

  // Filter client streams into |hal_streams| that will be requested to the HAL.
  client_streams_ = CopyToVector(stream_config->GetStreams());
  std::vector<camera3_stream_t*> hal_streams;

  for (auto* s : client_streams_) {
    if (s->format == HAL_PIXEL_FORMAT_BLOB) {
      // Check if existing stream for Portrait Mode effect.
      if (!IsPortraitModeStream(s, stream_effects_map)) {
        blob_stream_ = s;
        hal_streams.push_back(s);
      } else {
        portrait_blob_stream_ = s;
      }
    } else {
      hal_streams.push_back(s);
    }
  }
  if (portrait_blob_stream_) {
    // Process the portrait blob stream inplace.
    still_capture_processor_->Initialize(
        portrait_blob_stream_,
        base::BindPostTask(
            thread_.task_runner(),
            base::BindRepeating(&PortraitModeStreamManipulator::
                                    ReturnStillCaptureResultOnThread,
                                base::Unretained(this))));
    // Note that we don't bring the preview stream when sending a portrait mode
    // request. Always creates a YUV stream for the blob stream. The YUV stream
    // is fed to the StillImageProcessor to be compressed into JPEG blob.
    DCHECK_EQ(yuv_stream_for_portrait_blob_, nullptr);
    still_yuv_stream_ = std::make_unique<camera3_stream_t>(camera3_stream_t{
        .stream_type = CAMERA3_STREAM_OUTPUT,
        .width = portrait_blob_stream_->width,
        .height = portrait_blob_stream_->height,
        .format = HAL_PIXEL_FORMAT_YCbCr_420_888,
        .usage = GRALLOC_USAGE_SW_READ_OFTEN,
    });
    yuv_stream_for_portrait_blob_ = still_yuv_stream_.get();
    hal_streams.push_back(still_yuv_stream_.get());
  }

  if (!stream_config->SetStreams(hal_streams)) {
    LOGF(ERROR) << "Failed to manipulate stream config";
    return false;
  }

  if (VLOG_IS_ON(2)) {
    VLOGF(2) << "Config streams to HAL:";
    for (auto* s : stream_config->GetStreams()) {
      VLOGF(2) << "  " << GetDebugString(s);
    }
  }

  return true;
}

bool PortraitModeStreamManipulator::OnConfiguredStreamsOnThread(
    Camera3StreamConfiguration* stream_config) {
  CHECK(thread_.IsCurrentThread());
  TRACE_PORTRAIT_MODE();

  if (VLOG_IS_ON(2)) {
    VLOGF(2) << "Configured streams from HAL:";
    for (auto* s : stream_config->GetStreams()) {
      VLOGF(2) << "  " << GetDebugString(s);
    }
  }

  // Allocate a buffer pool for portrait blob stream which is created by us.
  if (still_yuv_stream_) {
    still_yuv_buffer_pool_ =
        std::make_unique<CameraBufferPool>(CameraBufferPool::Options{
            .width = still_yuv_stream_->width,
            .height = still_yuv_stream_->height,
            .format = base::checked_cast<uint32_t>(still_yuv_stream_->format),
            .usage = still_yuv_stream_->usage,
            .max_num_buffers =
                base::strict_cast<size_t>(still_yuv_stream_->max_buffers) + 1,
        });
  }

  // Set max buffers for the client streams not passed down.
  for (auto* s : client_streams_) {
    if (s == portrait_blob_stream_)
      s->max_buffers = 1;
  }

  // Restore client config.
  if (!stream_config->SetStreams(client_streams_)) {
    LOGF(ERROR) << "Failed to recover stream config";
    return false;
  }

  if (VLOG_IS_ON(2)) {
    VLOGF(2) << "Configured streams to client:";
    for (auto* s : stream_config->GetStreams()) {
      VLOGF(2) << "  " << GetDebugString(s);
    }
  }

  return true;
}

bool PortraitModeStreamManipulator::ProcessCaptureRequestOnThread(
    Camera3CaptureDescriptor* request) {
  CHECK(thread_.IsCurrentThread());
  TRACE_PORTRAIT_MODE("frame_number", request->frame_number());

  if (!portrait_mode_config_ || !IsPortraitModeRequest(request)) {
    return true;
  }

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, request->frame_number())
        << "Request stream buffers from client:";
    for (auto& b : request->GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream());
    }
  }

  CaptureContext* ctx = CreateCaptureContext(request->frame_number());
  if (!ctx) {
    return false;
  }

  // Process still capture.
  for (auto& b : request->AcquireOutputBuffers()) {
    if (b.stream() == portrait_blob_stream_) {
      ctx->has_pending_blob = true;
      if (request->HasMetadata(ANDROID_JPEG_ORIENTATION)) {
        ctx->orientation =
            request->GetMetadata<int32_t>(ANDROID_JPEG_ORIENTATION)[0];
      }
      still_capture_processor_->QueuePendingRequest(request->frame_number(),
                                                    *request);
      if (b.raw_buffer().buffer != nullptr) {
        still_capture_processor_->QueuePendingOutputBuffer(
            request->frame_number(), b.mutable_raw_buffer());
      }
    } else {
      request->AppendOutputBuffer(std::move(b));
    }
  }
  // Append a new YUV buffer output.
  if (ctx->has_pending_blob && still_yuv_stream_) {
    DCHECK_NE(still_yuv_buffer_pool_, nullptr);
    ctx->still_yuv_buffer = still_yuv_buffer_pool_->RequestBuffer();
    if (!ctx->still_yuv_buffer) {
      LOGF(ERROR) << "Failed to allocate YUV buffer for frame "
                  << request->frame_number();
      return false;
    }
    request->AppendOutputBuffer(Camera3StreamBuffer::MakeRequestOutput({
        .stream = still_yuv_stream_.get(),
        .buffer = ctx->still_yuv_buffer->handle(),
        .status = CAMERA3_BUFFER_STATUS_OK,
        .acquire_fence = -1,
        .release_fence = -1,
    }));
  }

  ctx->num_pending_buffers = request->num_output_buffers();

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, request->frame_number()) << "Request stream buffers to HAL:";
    for (auto& b : request->GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream());
    }
  }

  return true;
}

bool PortraitModeStreamManipulator::ProcessCaptureResultOnThread(
    Camera3CaptureDescriptor result) {
  CHECK(thread_.IsCurrentThread());
  TRACE_PORTRAIT_MODE("frame_number", result.frame_number());

  if (!portrait_mode_config_) {
    callbacks_.result_callback.Run(std::move(result));
    return true;
  }

  CaptureContext* ctx = GetCaptureContext(result.frame_number());
  if (!ctx) {
    // This capture is bypassed.
    callbacks_.result_callback.Run(std::move(result));
    return true;
  }

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, result.frame_number()) << "Result stream buffers from HAL:";
    for (auto& b : result.GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream());
    }
  }

  DCHECK_GE(ctx->num_pending_buffers, result.num_output_buffers());
  ctx->num_pending_buffers -= result.num_output_buffers();
  ctx->metadata_received |= result.partial_result() == partial_result_count_;

  base::ScopedClosureRunner ctx_deleter;
  if (ctx->num_pending_buffers == 0 && ctx->metadata_received &&
      !ctx->has_pending_blob && ctx->has_updated_metadata) {
    ctx_deleter.ReplaceClosure(
        base::BindOnce(&PortraitModeStreamManipulator::RemoveCaptureContext,
                       base::Unretained(this), result.frame_number()));
  }

  std::optional<Camera3StreamBuffer> still_yuv_buffer;
  for (auto& b : result.AcquireOutputBuffers()) {
    if (b.stream() == blob_stream_) {
      if (!still_capture_processor_->IsPendingOutputBufferQueued(
              result.frame_number())) {
        still_capture_processor_->QueuePendingOutputBuffer(
            result.frame_number(), b.mutable_raw_buffer());
      }
      // If this is a blob stream, extract its metadata.
      still_capture_processor_->QueuePendingAppsSegments(
          result.frame_number(), *b.buffer(),
          base::ScopedFD(b.take_release_fence()));
      result.AppendOutputBuffer(std::move(b));
    } else if (b.stream() == yuv_stream_for_portrait_blob_) {
      still_yuv_buffer = std::move(b);
    } else {
      result.AppendOutputBuffer(std::move(b));
    }
  }

  // Portrait Mode processing.
  if (ctx->has_pending_blob && still_yuv_buffer) {
    if (still_yuv_buffer->status() != CAMERA3_BUFFER_STATUS_OK) {
      VLOGF(1) << "Received still YUV buffer with error in result "
               << result.frame_number();
      return false;
    }
    // TODO(julianachang): Temporarily set can_process_portrait_mode to true.
    // This is necessary for the current function, but will be removed in the
    // follow-up CL.
    bool can_process_portrait_mode = true;
    SegmentationResult seg_result = SegmentationResult::kUnknown;
    if (portrait_mode_->ReprocessRequest(
            can_process_portrait_mode, *still_yuv_buffer->buffer(),
            ctx->orientation, &seg_result,
            *ctx->still_yuv_buffer->handle()) != 0) {
      LOGF(ERROR) << "Failed to apply Portrait Mode effect";
      return false;
    }
    still_capture_processor_->QueuePendingYuvImage(
        result.frame_number(), *ctx->still_yuv_buffer->handle(),
        base::ScopedFD());
    ctx->segmentation_result = seg_result;
    ctx->still_yuv_buffer = std::nullopt;
  }

  // Fill Portrait Mode segmentation result in metadata.
  if (ctx->segmentation_result.has_value() &&
      (ctx->pending_result_ || result.has_metadata())) {
    Camera3CaptureDescriptor* res =
        ctx->pending_result_ ? &ctx->pending_result_.value() : &result;
    SegmentationResult seg_result = *ctx->segmentation_result;
    if (seg_result == SegmentationResult::kUnknown ||
        !UpdateResultMetadata(res, seg_result)) {
      LOGF(ERROR) << "Cannot update kPortraitModeSegmentationResultVendorKey "
                     "in result "
                  << res->frame_number();
    }
    if (ctx->pending_result_) {
      callbacks_.result_callback.Run(std::move(ctx->pending_result_.value()));
      ctx->pending_result_.reset();
    }
    ctx->has_updated_metadata = true;
    ctx->segmentation_result.reset();
  }

  // Holds the last partial result if we have not updated the portrait mode
  // processing result to metadata yet.
  if (result.partial_result() == partial_result_count_ &&
      !ctx->has_updated_metadata) {
    // Returns the buffers to the client first if the result contains both
    // buffers and metadata.
    if (result.has_input_buffer() || result.num_output_buffers() > 0) {
      Camera3CaptureDescriptor buffer_result(camera3_capture_result_t{});
      if (result.has_input_buffer()) {
        buffer_result.SetInputBuffer(*result.AcquireInputBuffer());
      }
      buffer_result.SetOutputBuffers(result.AcquireOutputBuffers());
      callbacks_.result_callback.Run(std::move(buffer_result));
    }
    ctx->pending_result_ = std::move(result);
  } else {
    callbacks_.result_callback.Run(std::move(result));
  }

  return true;
}

void PortraitModeStreamManipulator::ReturnStillCaptureResultOnThread(
    Camera3CaptureDescriptor result) {
  CHECK(thread_.IsCurrentThread());
  TRACE_PORTRAIT_MODE();

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, result.frame_number()) << "Still capture result:";
    for (auto& b : result.GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream());
    }
  }

  CaptureContext* ctx = GetCaptureContext(result.frame_number());
  DCHECK_NE(ctx, nullptr);
  ctx->still_yuv_buffer = std::nullopt;
  ctx->has_pending_blob = false;
  if (ctx->num_pending_buffers == 0 && ctx->metadata_received &&
      !ctx->has_pending_blob && ctx->has_updated_metadata) {
    RemoveCaptureContext(result.frame_number());
  }

  callbacks_.result_callback.Run(std::move(result));
}

void PortraitModeStreamManipulator::ResetOnThread() {
  CHECK(thread_.IsCurrentThread());
  TRACE_PORTRAIT_MODE();

  still_capture_processor_->Reset();

  portrait_mode_config_.reset();
  client_streams_.clear();
  blob_stream_ = nullptr;
  portrait_blob_stream_ = nullptr;
  yuv_stream_for_portrait_blob_ = nullptr;
  still_yuv_stream_.reset();
  still_yuv_buffer_pool_.reset();
  capture_contexts_.clear();
}

bool PortraitModeStreamManipulator::IsPortraitModeStream(
    const camera3_stream_t* stream, const StreamEffectMap* stream_effects_map) {
  auto it = stream_effects_map->find(stream);
  if (it == stream_effects_map->end())
    return false;

  for (const auto& effect : it->second) {
    if (effect->type == StreamEffectType::kPortraitMode) {
      portrait_mode_config_ = PortraitModeConfig{
          .stream = stream,
          .enable_rectiface =
              static_cast<const PortraitModeStreamEffect*>(effect.get())
                  ->enable_rectiface,
      };
      return true;
    }
  }
  return false;
}

bool PortraitModeStreamManipulator::IsPortraitModeRequest(
    const Camera3CaptureDescriptor* request) {
  for (const auto& buffer : request->GetOutputBuffers()) {
    if (buffer.stream() == portrait_mode_config_->stream) {
      return true;
    }
  }
  return false;
}

PortraitModeStreamManipulator::CaptureContext*
PortraitModeStreamManipulator::CreateCaptureContext(uint32_t frame_number) {
  DCHECK(!base::Contains(capture_contexts_, frame_number));
  auto [it, is_inserted] = capture_contexts_.insert(
      std::make_pair(frame_number, std::make_unique<CaptureContext>()));
  if (!is_inserted) {
    LOGF(ERROR) << "Multiple captures with same frame number " << frame_number;
    return nullptr;
  }
  return it->second.get();
}

PortraitModeStreamManipulator::CaptureContext*
PortraitModeStreamManipulator::GetCaptureContext(uint32_t frame_number) const {
  auto it = capture_contexts_.find(frame_number);
  return it != capture_contexts_.end() ? it->second.get() : nullptr;
}

void PortraitModeStreamManipulator::RemoveCaptureContext(
    uint32_t frame_number) {
  capture_contexts_.erase(frame_number);
}

}  // namespace cros
