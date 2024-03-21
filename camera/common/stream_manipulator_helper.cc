/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/stream_manipulator_helper.h"

#include <camera/camera_metadata.h>
#include <hardware/camera3.h>
#include <hardware/gralloc.h>
#include <system/camera_metadata.h>
#include <system/graphics-base.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

#include <base/check.h>
#include <base/containers/fixed_flat_set.h>
#include <base/containers/span.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/memory/scoped_refptr.h>
#include <base/numerics/safe_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/synchronization/waitable_event.h>
#include <base/task/bind_post_task.h>
#include <base/task/sequenced_task_runner.h>

#include "common/camera_buffer_pool.h"
#include "common/camera_hal3_helpers.h"
#include "common/capture_result_sequencer.h"
#include "common/still_capture_processor.h"
#include "common/stream_manipulator.h"
#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/common.h"
#include "cros-camera/common_types.h"

namespace cros {
namespace {

constexpr int kSyncWaitTimeoutMs = 300;

std::vector<StreamFormat> GetAvailableOutputFormats(
    const camera_metadata_t* static_info, const Size& active_array_size) {
  auto min_durations = GetRoMetadataAsSpan<int64_t>(
      static_info, ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS);
  CHECK_EQ(min_durations.size() % 4, 0);
  auto get_max_fps = [&](uint32_t format, uint32_t width, uint32_t height) {
    for (size_t i = 0; i < min_durations.size(); i += 4) {
      if (format == min_durations[i] && width == min_durations[i + 1] &&
          height == min_durations[i + 2]) {
        const int64_t duration_ns = min_durations[i + 3];
        CHECK_GT(duration_ns, 0);
        return 1e9f / duration_ns;
      }
    }
    LOGF(FATAL) << "Min frame duration not found for format "
                << Size(width, height).ToString() << " " << format;
  };

  std::vector<StreamFormat> result;
  auto stream_configs = GetRoMetadataAsSpan<int32_t>(
      static_info, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
  CHECK_EQ(stream_configs.size() % 4, 0);
  for (size_t i = 0; i < stream_configs.size(); i += 4) {
    const uint32_t format = base::checked_cast<uint32_t>(stream_configs[i]);
    const uint32_t width = base::checked_cast<uint32_t>(stream_configs[i + 1]);
    const uint32_t height = base::checked_cast<uint32_t>(stream_configs[i + 2]);
    const uint32_t direction =
        base::checked_cast<uint32_t>(stream_configs[i + 3]);
    if (direction == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT &&
        (format == HAL_PIXEL_FORMAT_YCBCR_420_888 ||
         format == HAL_PIXEL_FORMAT_BLOB)) {
      result.push_back(StreamFormat{
          .width = width,
          .height = height,
          .format = format,
          .max_fps = get_max_fps(format, width, height),
          .fov = RelativeFov(Size(width, height), active_array_size),
      });
    }
  }
  return result;
}

bool IsOutputFormatYuv(uint32_t format) {
  return format == HAL_PIXEL_FORMAT_YCBCR_420_888 ||
         format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
}

// Check if the source stream can generate the destination stream by
// crop-scaling. Return the scaling factor if true.
std::optional<float> GetScalingFactor(const StreamFormat& src_format,
                                      const StreamFormat& dst_format,
                                      bool for_still_capture) {
  // Strictly speaking we can't generate stream from lower fps to higher fps,
  // but in practice we only stream in video and photo speeds. Quantize the
  // frame rates to allow more formats to be generated.
  constexpr float kMinVideoFps = 29.9f;
  auto index_fps = [](float fps) { return fps >= kMinVideoFps ? 1 : 0; };
  if (src_format.format == HAL_PIXEL_FORMAT_BLOB ||
      !src_format.fov.Covers(dst_format.fov) ||
      (!for_still_capture &&
       index_fps(src_format.max_fps) < index_fps(dst_format.max_fps))) {
    return std::nullopt;
  }
  return std::max(static_cast<float>(dst_format.width) /
                      static_cast<float>(src_format.width),
                  static_cast<float>(dst_format.height) /
                      static_cast<float>(src_format.height));
}

bool CopyMetadataTag(uint32_t tag,
                     const android::CameraMetadata& src,
                     android::CameraMetadata& dst) {
  const camera_metadata_t* src_locked = src.getAndLock();
  base::ScopedClosureRunner src_unlocker(
      base::BindOnce(base::IgnoreResult(&android::CameraMetadata::unlock),
                     base::Unretained(&src), src_locked));

  camera_metadata_ro_entry entry = {};
  if (find_camera_metadata_ro_entry(src_locked, tag, &entry) != 0) {
    return false;
  }
  return dst.update(entry) == 0;
}

bool MoveMetadataTag(uint32_t tag,
                     android::CameraMetadata& src,
                     android::CameraMetadata& dst) {
  return CopyMetadataTag(tag, src, dst) && src.erase(tag) == 0;
}

void SetBufferError(Camera3StreamBuffer& buffer) {
  buffer.mutable_raw_buffer().status = CAMERA3_BUFFER_STATUS_ERROR;
  base::ScopedFD(buffer.take_acquire_fence());
}

}  // namespace

StreamManipulatorHelper::StreamManipulatorHelper(
    Config config,
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks,
    OnProcessTaskCallback on_process_task,
    CropScaleImageCallback crop_scale_image,
    std::unique_ptr<StillCaptureProcessor> still_capture_processor,
    scoped_refptr<base::SequencedTaskRunner> task_runner)
    : config_(std::move(config)),
      result_sequencer_(
          std::make_unique<CaptureResultSequencer>(std::move(callbacks))),
      on_process_task_(std::move(on_process_task)),
      crop_scale_image_(std::move(crop_scale_image)),
      still_capture_processor_(std::move(still_capture_processor)),
      task_runner_(std::move(task_runner)) {
  CHECK(!on_process_task_.is_null());
  CHECK(!crop_scale_image_.is_null());
  CHECK_NE(still_capture_processor_, nullptr);
  CHECK_NE(task_runner_, nullptr);
  CHECK_NE(static_info, nullptr);

  partial_result_count_ = base::checked_cast<uint32_t>(
      GetRoMetadata<int32_t>(static_info, ANDROID_REQUEST_PARTIAL_RESULT_COUNT)
          .value_or(1));

  active_array_size_ = [&]() {
    auto values = GetRoMetadataAsSpan<int32_t>(
        static_info, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);
    CHECK_EQ(values.size(), 4);
    return Size(base::checked_cast<uint32_t>(values[2]),
                base::checked_cast<uint32_t>(values[3]));
  }();

  available_formats_ =
      GetAvailableOutputFormats(static_info, active_array_size_);
}

StreamManipulatorHelper::~StreamManipulatorHelper() {
  task_runner_->DeleteSoon(FROM_HERE, std::move(result_sequencer_));
}

bool StreamManipulatorHelper::PreConfigure(
    Camera3StreamConfiguration* stream_config) {
  if (!task_runner_->RunsTasksInCurrentSequence()) {
    bool ret = false;
    base::WaitableEvent done;
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&StreamManipulatorHelper::PreConfigure,
                       base::Unretained(this), base::Unretained(stream_config))
            .Then(base::BindOnce(
                [](base::WaitableEvent* done, bool* ret, bool result) {
                  *ret = result;
                  done->Signal();
                },
                base::Unretained(&done), base::Unretained(&ret))));
    done.Wait();
    return ret;
  }

  Reset();

  if (config_.process_mode == ProcessMode::kBypass) {
    return true;
  }

  camera3_stream_t* blob_stream = nullptr;
  camera3_stream_t* still_yuv_stream = nullptr;
  std::vector<camera3_stream_t*> video_yuv_streams;
  std::vector<camera3_stream_t*> ignored_streams;
  for (auto* s : stream_config->GetStreams()) {
    if ((stream_config->stream_effects_map() != nullptr &&
         stream_config->stream_effects_map()->contains(s)) ||
        s->stream_type != CAMERA3_STREAM_OUTPUT ||
        (s->usage & GRALLOC_USAGE_HW_CAMERA_READ)) {
      // Ignore feature-specific and input/bidirectional streams.
      ignored_streams.push_back(s);
    } else if (s->format == HAL_PIXEL_FORMAT_BLOB) {
      CHECK_EQ(blob_stream, nullptr) << "Multiple BLOB streams configured";
      blob_stream = s;
    } else {
      CHECK(IsOutputFormatYuv(s->format))
          << "Stream with unexpected format configured: " << GetDebugString(s);
      if (s->usage & kStillCaptureUsageFlag) {
        CHECK_EQ(still_yuv_stream, nullptr)
            << "Multiple still YUV streams configured";
        still_yuv_stream = s;
      } else {
        video_yuv_streams.push_back(s);
      }
    }
  }

  // Configure still capture streams.
  if (still_yuv_stream != nullptr) {
    CHECK_NE(blob_stream, nullptr);
    CHECK(GetFormat(*still_yuv_stream).fov.Covers(GetFormat(*blob_stream).fov));
  }
  if (blob_stream != nullptr) {
    blob_size_ = Size(blob_stream->width, blob_stream->height);
    still_capture_processor_->Initialize(
        blob_stream,
        base::BindPostTask(
            task_runner_,
            base::BindRepeating(&StreamManipulatorHelper::OnStillCaptureResult,
                                base::Unretained(this))));
    std::vector<camera3_stream_t*> still_streams = {blob_stream};
    if (still_yuv_stream != nullptr) {
      still_streams.push_back(still_yuv_stream);
    }
    std::optional<ConfiguredState> configured_state =
        ConfigureStreamsAndFindSourceStream(still_streams,
                                            /*for_still_capture=*/true);
    if (!configured_state.has_value()) {
      LOGF(WARNING) << "Stream config unsupported for still processing; "
                       "bypassing this stream manipulator";
      Reset();
      stream_config_unsupported_ = true;
      return false;
    }
    still_process_input_stream_ = configured_state->src_stream;
    still_process_output_size_.emplace(
        still_yuv_stream != nullptr
            ? Size(still_yuv_stream->width, still_yuv_stream->height)
            : Size(still_process_input_stream_->ptr()->width,
                   still_process_input_stream_->ptr()->height)
                  .Scale(std::min(configured_state->max_scaling_factor, 1.0f)));
    // Create fake stream/format for convenience that we can create
    // Camera3StreamBuffer and look up formats.
    fake_still_process_output_stream_.emplace(camera3_stream_t{
        .stream_type = CAMERA3_STREAM_OUTPUT,
        .width = still_process_output_size_->width,
        .height = still_process_output_size_->height,
        .format = HAL_PIXEL_FORMAT_YCBCR_420_888,
        .usage = kProcessStreamUsageFlags | kStillCaptureUsageFlag,
    });
    fake_still_process_output_format_.emplace(StreamFormat{
        .width = still_process_output_size_->width,
        .height = still_process_output_size_->height,
        .format = HAL_PIXEL_FORMAT_YCBCR_420_888,
        .max_fps = 1.0f,
        .fov =
            RelativeFov(still_process_output_size_.value(), active_array_size_),
    });
  }

  // Configure video YUV streams.
  if (config_.process_mode == ProcessMode::kVideoAndStillProcess &&
      !video_yuv_streams.empty()) {
    std::optional<ConfiguredState> configured_state =
        ConfigureStreamsAndFindSourceStream(video_yuv_streams,
                                            /*for_still_capture=*/false);
    if (!configured_state.has_value()) {
      LOGF(WARNING) << "Stream config unsupported for video processing; "
                       "bypassing this stream manipulator";
      Reset();
      stream_config_unsupported_ = true;
      return false;
    }
    video_process_input_stream_ = configured_state->src_stream;
    // If preferring large source size, let the process tasks output to a
    // smaller size.
    video_process_output_size_.emplace(
        Size(video_process_input_stream_->ptr()->width,
             video_process_input_stream_->ptr()->height)
            .Scale(std::min(configured_state->max_scaling_factor, 1.0f)));
    // Create fake stream/format for convenience that we can create
    // Camera3StreamBuffer and look up formats.
    fake_video_process_output_stream_.emplace(camera3_stream_t{
        .stream_type = CAMERA3_STREAM_OUTPUT,
        .width = video_process_output_size_->width,
        .height = video_process_output_size_->height,
        .format = HAL_PIXEL_FORMAT_YCBCR_420_888,
        .usage = kProcessStreamUsageFlags | kStillCaptureUsageFlag,
    });
    fake_video_process_output_format_.emplace(StreamFormat{
        .width = video_process_output_size_->width,
        .height = video_process_output_size_->height,
        .format = HAL_PIXEL_FORMAT_YCBCR_420_888,
        .max_fps = 1.0f,
        .fov =
            RelativeFov(video_process_output_size_.value(), active_array_size_),
    });
  }

  // Record client stream usages.
  if (blob_stream != nullptr) {
    client_stream_to_type_[blob_stream] = StreamType::kBlob;
  }
  if (still_yuv_stream != nullptr) {
    CHECK(still_process_input_stream_.has_value());
    client_stream_to_type_[still_yuv_stream] =
        still_process_input_stream_->ptr() == still_yuv_stream
            ? StreamType::kStillYuvToProcess
            : StreamType::kStillYuvToGenerate;
  }
  for (auto* s : video_yuv_streams) {
    client_stream_to_type_[s] = video_process_input_stream_.has_value()
                                    ? (video_process_input_stream_->ptr() == s
                                           ? StreamType::kVideoYuvToProcess
                                           : StreamType::kVideoYuvToGenerate)
                                    : StreamType::kIgnored;
  }
  for (auto* s : ignored_streams) {
    client_stream_to_type_[s] = StreamType::kIgnored;
  }

  // Manipulate streams to configure.
  base::flat_set<camera3_stream_t*> streams_to_configure(
      ignored_streams.begin(), ignored_streams.end());
  if (blob_stream != nullptr) {
    streams_to_configure.insert(blob_stream);
  }
  if (still_process_input_stream_.has_value()) {
    streams_to_configure.insert(still_process_input_stream_->ptr());
  } else if (still_yuv_stream != nullptr) {
    streams_to_configure.insert(still_yuv_stream);
  }
  if (video_process_input_stream_.has_value()) {
    streams_to_configure.insert(video_process_input_stream_->ptr());
  } else {
    streams_to_configure.insert(video_yuv_streams.begin(),
                                video_yuv_streams.end());
  }
  CHECK(stream_config->SetStreams(streams_to_configure));

  return true;
}

void StreamManipulatorHelper::PostConfigure(
    Camera3StreamConfiguration* stream_config) {
  if (!task_runner_->RunsTasksInCurrentSequence()) {
    base::WaitableEvent done;
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&StreamManipulatorHelper::PostConfigure,
                       base::Unretained(this), base::Unretained(stream_config))
            .Then(base::BindOnce(&base::WaitableEvent::Signal,
                                 base::Unretained(&done))));
    done.Wait();
    return;
  }

  if (config_.process_mode == ProcessMode::kBypass ||
      stream_config_unsupported_) {
    return;
  }

  // Create buffer pools.
  if (still_process_input_stream_.has_value()) {
    const camera3_stream_t* s = still_process_input_stream_->ptr();
    still_process_input_pool_ =
        std::make_unique<CameraBufferPool>(CameraBufferPool::Options{
            .width = s->width,
            .height = s->height,
            .format = base::checked_cast<uint32_t>(s->format),
            .usage = s->usage,
            .max_num_buffers = s->max_buffers,
        });
    still_process_output_pool_ =
        std::make_unique<CameraBufferPool>(CameraBufferPool::Options{
            .width = still_process_output_size_->width,
            .height = still_process_output_size_->height,
            .format = HAL_PIXEL_FORMAT_YCBCR_420_888,
            .usage = kProcessStreamUsageFlags,
            .max_num_buffers = s->max_buffers,
        });
    if (blob_size_.value() != still_process_output_size_.value()) {
      blob_sized_buffer_pool_ =
          std::make_unique<CameraBufferPool>(CameraBufferPool::Options{
              .width = blob_size_->width,
              .height = blob_size_->height,
              .format = HAL_PIXEL_FORMAT_YCBCR_420_888,
              .usage = kProcessStreamUsageFlags,
              .max_num_buffers = s->max_buffers,
          });
    }
  }
  if (video_process_input_stream_.has_value()) {
    const camera3_stream_t* s = video_process_input_stream_->ptr();
    video_process_input_pool_ =
        std::make_unique<CameraBufferPool>(CameraBufferPool::Options{
            .width = s->width,
            .height = s->height,
            .format = base::checked_cast<uint32_t>(s->format),
            .usage = s->usage,
            .max_num_buffers = s->max_buffers,
        });
    video_process_output_pool_ =
        std::make_unique<CameraBufferPool>(CameraBufferPool::Options{
            .width = video_process_output_size_->width,
            .height = video_process_output_size_->height,
            .format = HAL_PIXEL_FORMAT_YCBCR_420_888,
            .usage = kProcessStreamUsageFlags,
            .max_num_buffers = s->max_buffers,
        });
  }

  std::vector<camera3_stream_t*> streams;
  for (auto& [s, t] : client_stream_to_type_) {
    // Configure the streams that are not passed down.
    switch (t) {
      case StreamType::kStillYuvToGenerate:
        CHECK(still_process_input_stream_.has_value());
        s->usage |= kProcessStreamUsageFlags;
        s->max_buffers = still_process_input_stream_->ptr()->max_buffers;
        break;
      case StreamType::kVideoYuvToGenerate:
        CHECK(video_process_input_stream_.has_value());
        s->usage |= kProcessStreamUsageFlags;
        s->max_buffers = video_process_input_stream_->ptr()->max_buffers;
        break;
      default:
        break;
    }
    streams.push_back(s);
  }
  CHECK(stream_config->SetStreams(streams));
}

void StreamManipulatorHelper::HandleRequest(
    Camera3CaptureDescriptor* request,
    bool bypass_process,
    std::unique_ptr<PrivateContext> private_context) {
  if (!task_runner_->RunsTasksInCurrentSequence()) {
    base::WaitableEvent done;
    task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&StreamManipulatorHelper::HandleRequest,
                       base::Unretained(this), base::Unretained(request),
                       bypass_process, std::move(private_context))
            .Then(base::BindOnce(&base::WaitableEvent::Signal,
                                 base::Unretained(&done))));
    done.Wait();
    return;
  }

  result_sequencer_->AddRequest(*request);

  if (config_.process_mode == ProcessMode::kBypass ||
      stream_config_unsupported_) {
    return;
  }

  auto [it, ctx_added] = capture_contexts_.emplace(
      request->frame_number(), std::make_unique<CaptureContext>());
  CHECK(ctx_added);
  CaptureContext& capture_ctx = *it->second;
  capture_ctx.private_context = std::move(private_context);

  bool has_blob = false;
  std::optional<Camera3StreamBuffer> still_yuv_buffer_to_process;
  std::optional<Camera3StreamBuffer> still_yuv_buffer_to_generate;
  std::optional<Camera3StreamBuffer> video_yuv_buffer_to_process;
  std::vector<Camera3StreamBuffer> video_yuv_buffers_to_generate;
  if (request->has_input_buffer()) {
    const camera3_stream_t* s = request->GetInputBuffer()->stream();
    CHECK(client_stream_to_type_.contains(s));
    CHECK_EQ(client_stream_to_type_.at(s), StreamType::kIgnored);
    capture_ctx.requested_streams[s] = StreamContext{};
  }
  for (auto& b : request->AcquireOutputBuffers()) {
    CHECK(client_stream_to_type_.contains(b.stream()));
    switch (client_stream_to_type_.at(b.stream())) {
      case StreamType::kIgnored:
        capture_ctx.requested_streams[b.stream()] = StreamContext{};
        request->AppendOutputBuffer(std::move(b));
        break;
      case StreamType::kBlob:
        has_blob = true;
        if (!bypass_process) {
          still_capture_processor_->QueuePendingRequest(request->frame_number(),
                                                        *request);
        }
        capture_ctx.requested_streams[b.stream()] =
            StreamContext{.for_process = !bypass_process};
        request->AppendOutputBuffer(std::move(b));
        break;
      case StreamType::kStillYuvToProcess: {
        CHECK_EQ(b.stream(), still_process_input_stream_->ptr());
        CHECK(!still_yuv_buffer_to_process.has_value());
        still_yuv_buffer_to_process.emplace(std::move(b));
        break;
      }
      case StreamType::kStillYuvToGenerate:
        CHECK(!still_yuv_buffer_to_generate.has_value());
        still_yuv_buffer_to_generate.emplace(std::move(b));
        break;
      case StreamType::kVideoYuvToProcess: {
        CHECK_EQ(b.stream(), video_process_input_stream_->ptr());
        CHECK(!video_yuv_buffer_to_process.has_value());
        video_yuv_buffer_to_process.emplace(std::move(b));
        break;
      }
      case StreamType::kVideoYuvToGenerate:
        video_yuv_buffers_to_generate.push_back(std::move(b));
        break;
    }
  }

  // Setup still YUV stream for processing or generating other streams.
  CHECK(has_blob || (!still_yuv_buffer_to_process.has_value() &&
                     !still_yuv_buffer_to_generate.has_value()));
  if ((has_blob && !bypass_process) ||
      still_yuv_buffer_to_process.has_value() ||
      still_yuv_buffer_to_generate.has_value()) {
    StreamContext& stream_ctx =
        capture_ctx.requested_streams[still_process_input_stream_->ptr()];
    if (bypass_process && still_yuv_buffer_to_process.has_value()) {
      request->AppendOutputBuffer(
          std::move(still_yuv_buffer_to_process.value()));
    } else {
      stream_ctx.for_process = !bypass_process;
      stream_ctx.pool_process_input =
          still_process_input_pool_->RequestBuffer();
      CHECK(stream_ctx.pool_process_input.has_value());
      request->AppendOutputBuffer(
          Camera3StreamBuffer::MakeRequestOutput(camera3_stream_buffer_t{
              .stream = still_process_input_stream_->ptr(),
              .buffer = stream_ctx.pool_process_input->handle(),
              .status = CAMERA3_BUFFER_STATUS_OK,
              .acquire_fence = -1,
              .release_fence = -1,
          }));
      if (still_yuv_buffer_to_process.has_value()) {
        stream_ctx.process_output.emplace(
            std::move(still_yuv_buffer_to_process.value()));
      }
    }
    if (still_yuv_buffer_to_generate.has_value()) {
      stream_ctx.client_yuv_buffers_to_generate.push_back(
          std::move(still_yuv_buffer_to_generate.value()));
    }
  }

  // Setup video YUV stream for processing or generating other streams.
  if (video_yuv_buffer_to_process.has_value() ||
      !video_yuv_buffers_to_generate.empty()) {
    StreamContext& stream_ctx =
        capture_ctx.requested_streams[video_process_input_stream_->ptr()];
    if (bypass_process && video_yuv_buffer_to_process.has_value()) {
      request->AppendOutputBuffer(
          std::move(video_yuv_buffer_to_process.value()));
    } else {
      stream_ctx.for_process = !bypass_process;
      stream_ctx.pool_process_input =
          video_process_input_pool_->RequestBuffer();
      CHECK(stream_ctx.pool_process_input.has_value());
      request->AppendOutputBuffer(
          Camera3StreamBuffer::MakeRequestOutput(camera3_stream_buffer_t{
              .stream = video_process_input_stream_->ptr(),
              .buffer = stream_ctx.pool_process_input->handle(),
              .status = CAMERA3_BUFFER_STATUS_OK,
              .acquire_fence = -1,
              .release_fence = -1,
          }));
      if (video_yuv_buffer_to_process.has_value()) {
        stream_ctx.process_output.emplace(
            std::move(video_yuv_buffer_to_process.value()));
      }
    }
    stream_ctx.client_yuv_buffers_to_generate =
        std::move(video_yuv_buffers_to_generate);
  }
}

void StreamManipulatorHelper::HandleResult(Camera3CaptureDescriptor result) {
  if (!task_runner_->RunsTasksInCurrentSequence()) {
    task_runner_->PostTask(
        FROM_HERE, base::BindOnce(&StreamManipulatorHelper::HandleResult,
                                  base::Unretained(this), std::move(result)));
    return;
  }

  if (config_.process_mode == ProcessMode::kBypass ||
      stream_config_unsupported_) {
    result_sequencer_->AddResult(std::move(result));
    return;
  }

  auto [capture_ctx, capture_ctx_remover] =
      GetCaptureContext(result.frame_number());

  // Hold feature metadata until it's updated by process tasks.
  if (result.feature_metadata().faces.has_value()) {
    capture_ctx.feature_metadata.faces.swap(result.feature_metadata().faces);
    result.feature_metadata().faces.reset();
  }
  if (result.feature_metadata().hdr_ratio.has_value()) {
    capture_ctx.feature_metadata.hdr_ratio.swap(
        result.feature_metadata().hdr_ratio);
    result.feature_metadata().hdr_ratio.reset();
  }

  // Move result metadata to be updated into capture context.
  bool result_metadata_ready = true;
  for (uint32_t tag : config_.result_metadata_tags_to_update) {
    if (capture_ctx.result_metadata.exists(tag)) {
      if (result.HasMetadata(tag)) {
        LOGF(WARNING) << "Duplicated metadata tag "
                      << base::StringPrintf("0x%x", tag) << " in result "
                      << result.frame_number() << "; removed";
        CHECK(result.DeleteMetadata(tag));
      }
    } else if (!MoveMetadataTag(tag, result.mutable_metadata(),
                                capture_ctx.result_metadata)) {
      result_metadata_ready = false;
    }
  }
  if (result.partial_result() == partial_result_count_) {
    CHECK(!capture_ctx.last_result_metadata_received);
    capture_ctx.last_result_metadata_received = true;
    if (!config_.result_metadata_tags_to_update.empty()) {
      // Keep the last metadata packet to hold the updated metadata by
      // processing tasks.
      if (result.has_metadata()) {
        android::CameraMetadata m = result.ReleaseMetadata();
        CHECK_EQ(m.append(capture_ctx.result_metadata), 0);
        capture_ctx.result_metadata.acquire(m);
      }
      result.SetPartialResult(0);
    }
  }

  if (result.has_input_buffer()) {
    const camera3_stream_t* s = result.GetInputBuffer()->stream();
    CHECK(capture_ctx.requested_streams.contains(s));
    CHECK_EQ(capture_ctx.requested_streams[s].state, StreamState::kRequesting);
    capture_ctx.requested_streams[s].state = StreamState::kDone;
  }
  for (auto& b : result.AcquireOutputBuffers()) {
    auto it = capture_ctx.requested_streams.find(b.stream());
    CHECK(it != capture_ctx.requested_streams.end());
    StreamContext& stream_ctx = it->second;
    CHECK_EQ(stream_ctx.state, StreamState::kRequesting);
    std::optional<Camera3StreamBuffer> error_buffer;
    if (stream_ctx.for_process) {
      if (b.stream()->format == HAL_PIXEL_FORMAT_BLOB) {
        // BLOB for processing.
        if (capture_ctx.still_capture_cancelled) {
          stream_ctx.state = StreamState::kDone;
          SetBufferError(b);
          result.AppendOutputBuffer(std::move(b));
        } else if (b.status() != CAMERA3_BUFFER_STATUS_OK) {
          stream_ctx.state = StreamState::kDone;
          capture_ctx.still_capture_cancelled = true;
          still_capture_processor_->CancelPendingRequest(result.frame_number());
          result.AppendOutputBuffer(std::move(b));
          if (capture_ctx.client_buffer_for_blob.has_value()) {
            result.AppendOutputBuffer(
                std::move(capture_ctx.client_buffer_for_blob.value()));
            capture_ctx.client_buffer_for_blob.reset();
          }
          if (capture_ctx.pool_buffer_for_blob.has_value()) {
            capture_ctx.pool_buffer_for_blob.reset();
          }
        } else {
          stream_ctx.state = StreamState::kProcessing;
          still_capture_processor_->QueuePendingAppsSegments(
              result.frame_number(), *b.buffer(),
              base::ScopedFD(b.take_release_fence()));
          still_capture_processor_->QueuePendingOutputBuffer(
              result.frame_number(), b.raw_buffer());
        }
      } else if (b.stream()->usage & kStillCaptureUsageFlag) {
        // Still YUV for processing.
        if (b.status() != CAMERA3_BUFFER_STATUS_OK) {
          stream_ctx.state = StreamState::kDone;
          if (!capture_ctx.still_capture_cancelled) {
            capture_ctx.still_capture_cancelled = true;
            still_capture_processor_->CancelPendingRequest(
                result.frame_number());
          }
          error_buffer.emplace(std::move(b));
        } else {
          stream_ctx.state = StreamState::kPending;
          stream_ctx.process_input.emplace(std::move(b));
        }
      } else {
        // Video YUV for processing.
        if (b.status() != CAMERA3_BUFFER_STATUS_OK) {
          stream_ctx.state = StreamState::kDone;
          error_buffer.emplace(std::move(b));
        } else {
          stream_ctx.state = StreamState::kPending;
          stream_ctx.process_input.emplace(std::move(b));
        }
      }
    } else {
      // No processing.
      stream_ctx.state = StreamState::kDone;
      if (b.status() != CAMERA3_BUFFER_STATUS_OK) {
        error_buffer.emplace(std::move(b));
      } else {
        CropScaleImages(b, stream_ctx.client_yuv_buffers_to_generate);
        if (stream_ctx.pool_process_input.has_value()) {
          if (!b.WaitOnAndClearReleaseFence(kSyncWaitTimeoutMs)) {
            LOGF(WARNING) << "Sync wait timed out on result "
                          << result.frame_number() << " ("
                          << GetDebugString(b.stream()) << ")";
          }
          stream_ctx.pool_process_input.reset();
        } else {
          result.AppendOutputBuffer(std::move(b));
        }
        for (auto& bb : stream_ctx.client_yuv_buffers_to_generate) {
          result.AppendOutputBuffer(std::move(bb));
        }
        stream_ctx.client_yuv_buffers_to_generate.clear();
      }
    }
    if (error_buffer.has_value()) {
      CHECK_NE(error_buffer->status(), CAMERA3_BUFFER_STATUS_OK);
      if (stream_ctx.pool_process_input.has_value()) {
        stream_ctx.pool_process_input.reset();
      } else {
        result.AppendOutputBuffer(std::move(error_buffer.value()));
      }
      if (stream_ctx.process_output.has_value()) {
        SetBufferError(stream_ctx.process_output.value());
        result.AppendOutputBuffer(std::move(stream_ctx.process_output.value()));
      }
      for (auto& bb : stream_ctx.client_yuv_buffers_to_generate) {
        SetBufferError(bb);
        result.AppendOutputBuffer(std::move(bb));
      }
      stream_ctx.client_yuv_buffers_to_generate.clear();
    }
  }

  // Send process tasks.
  if (result_metadata_ready || capture_ctx.last_result_metadata_received) {
    for (auto& [s, stream_ctx] : capture_ctx.requested_streams) {
      if (stream_ctx.state != StreamState::kPending) {
        continue;
      }
      CHECK(stream_ctx.for_process);
      CHECK(stream_ctx.process_input.has_value());
      stream_ctx.state = StreamState::kProcessing;
      if (!stream_ctx.process_output.has_value()) {
        const bool is_still = s->usage & kStillCaptureUsageFlag;
        // Try to find an output buffer from client buffers. Allocate from
        // buffer pool if not found.
        auto it = std::find_if(
            stream_ctx.client_yuv_buffers_to_generate.begin(),
            stream_ctx.client_yuv_buffers_to_generate.end(),
            [target_size = is_still ? still_process_output_size_
                                    : video_process_output_size_](auto& b) {
              return Size(b.stream()->width, b.stream()->height) == target_size;
            });
        if (it != stream_ctx.client_yuv_buffers_to_generate.end()) {
          stream_ctx.process_output.emplace(std::move(*it));
          stream_ctx.client_yuv_buffers_to_generate.erase(it);
        } else {
          stream_ctx.pool_process_output =
              is_still ? still_process_output_pool_->RequestBuffer()
                       : video_process_output_pool_->RequestBuffer();
          CHECK(stream_ctx.pool_process_output.has_value());
          // Create a fake Camera3StreamBuffer for convenience.
          stream_ctx.process_output.emplace(
              Camera3StreamBuffer::MakeRequestOutput(camera3_stream_buffer_t{
                  .stream = is_still
                                ? &fake_still_process_output_stream_.value()
                                : &fake_video_process_output_stream_.value(),
                  .buffer = stream_ctx.pool_process_output->handle(),
                  .status = CAMERA3_BUFFER_STATUS_OK,
                  .acquire_fence = -1,
                  .release_fence = -1,
              }));
        }
      }
      on_process_task_.Run(ScopedProcessTask(
          new ProcessTask(
              result.frame_number(), &stream_ctx.process_input.value(),
              &stream_ctx.process_output.value(), &capture_ctx.result_metadata,
              &capture_ctx.feature_metadata, capture_ctx.private_context.get(),
              base::BindOnce(&StreamManipulatorHelper::OnProcessTaskDone,
                             base::Unretained(this))),
          base::OnTaskRunnerDeleter(task_runner_)));
    }
  }

  ReturnCaptureResult(std::move(result), capture_ctx);
}

void StreamManipulatorHelper::Notify(camera3_notify_msg_t msg) {
  if (!task_runner_->RunsTasksInCurrentSequence()) {
    task_runner_->PostTask(FROM_HERE,
                           base::BindOnce(&StreamManipulatorHelper::Notify,
                                          base::Unretained(this), msg));
    return;
  }

  if (msg.type == CAMERA3_MSG_ERROR) {
    const camera3_error_msg_t& err = msg.message.error;
    switch (err.error_code) {
      case CAMERA3_MSG_ERROR_DEVICE:
        break;
      case CAMERA3_MSG_ERROR_REQUEST:
        // By spec the buffers will be returned with error status, so nothing to
        // do here.
        break;
      case CAMERA3_MSG_ERROR_BUFFER:
        // TODO(kamesan): Handle this case.
        LOGF(ERROR) << "Received buffer error message on frame "
                    << err.frame_number << " ("
                    << GetDebugString(err.error_stream) << ")";
        break;
      default:
        break;
    }
  }
  result_sequencer_->Notify(msg);
}

void StreamManipulatorHelper::Flush() {
  // TODO(kamesan): Implement.
}

const StreamFormat& StreamManipulatorHelper::GetFormat(
    const camera3_stream_t& stream) const {
  CHECK(task_runner_->RunsTasksInCurrentSequence());
  CHECK_EQ(stream.stream_type, CAMERA3_STREAM_OUTPUT);

  if (fake_still_process_output_stream_.has_value() &&
      &fake_still_process_output_stream_.value() == &stream) {
    return fake_still_process_output_format_.value();
  }
  if (fake_video_process_output_stream_.has_value() &&
      &fake_video_process_output_stream_.value() == &stream) {
    return fake_video_process_output_format_.value();
  }

  auto it = std::find_if(
      available_formats_.begin(), available_formats_.end(), [&](auto& f) {
        return f.width == stream.width && f.height == stream.height &&
               (f.format == stream.format ||
                (IsOutputFormatYuv(f.format) &&
                 IsOutputFormatYuv(stream.format)));
      });
  CHECK(it != available_formats_.end());
  return *it;
}

std::optional<StreamManipulatorHelper::ConfiguredState>
StreamManipulatorHelper::ConfigureStreamsAndFindSourceStream(
    base::span<camera3_stream_t* const> dst_streams,
    bool for_still_capture) const {
  CHECK(task_runner_->RunsTasksInCurrentSequence());
  CHECK(!dst_streams.empty());

  uint32_t src_max_buffers = dst_streams[0]->max_buffers;
  int crop_rotate_scale_degrees = dst_streams[0]->crop_rotate_scale_degrees;
  for (auto* s : dst_streams) {
    CHECK(s->physical_camera_id == nullptr || s->physical_camera_id[0] == '\0');
    CHECK_EQ(crop_rotate_scale_degrees, s->crop_rotate_scale_degrees);
    src_max_buffers = std::max(src_max_buffers, s->max_buffers);
    if (IsOutputFormatYuv(s->format)) {
      s->usage |= kProcessStreamUsageFlags;
    }
  }

  base::flat_map<const StreamFormat*, camera3_stream_t*> format_to_dst_stream;
  for (auto* s : dst_streams) {
    format_to_dst_stream[&GetFormat(*s)] = s;
  }

  auto get_max_scaling_factor =
      [&](const StreamFormat& src_format) -> std::optional<float> {
    std::optional<float> result;
    for (auto& [dst_format, s] : format_to_dst_stream) {
      const std::optional<float> scaling_factor =
          GetScalingFactor(src_format, *dst_format, for_still_capture);
      if (!scaling_factor.has_value()) {
        return std::nullopt;
      }
      result.emplace(result.has_value()
                         ? std::max(result.value(), scaling_factor.value())
                         : scaling_factor.value());
    }
    return result;
  };
  auto index_format = [&](const StreamFormat& f) {
    // Prefer generating destination streams without upscaling, and prefer
    // choosing source stream from destination streams over creating a new
    // stream.
    const std::optional<float> max_scaling_factor = get_max_scaling_factor(f);
    return std::make_tuple(
        max_scaling_factor.has_value()
            ? (config_.prefer_large_source
                   ? 1.0f / max_scaling_factor.value()
                   : std::min(1.0f, 1.0f / max_scaling_factor.value()))
            : 0.0f,
        format_to_dst_stream.count(&f), -f.width, -f.height, f.max_fps);
  };
  auto it = std::max_element(
      available_formats_.begin(), available_formats_.end(),
      [&](auto& f1, auto& f2) { return index_format(f1) < index_format(f2); });
  CHECK(it != available_formats_.end());
  const StreamFormat& src_format = *it;
  const std::optional<float> max_scaling_factor =
      get_max_scaling_factor(src_format);
  if (!max_scaling_factor.has_value()) {
    return std::nullopt;
  }
  if (max_scaling_factor.value() > 1.0f) {
    LOGF(WARNING) << "Upscaling "
                  << Size(src_format.width, src_format.height).ToString()
                  << " stream to generate other streams";
  }
  constexpr const char* kEmptyPhysicalCameraId = "";
  return ConfiguredState{
      .src_stream =
          format_to_dst_stream.contains(&src_format)
              ? OwnedOrExternalStream(format_to_dst_stream[&src_format])
              : OwnedOrExternalStream(camera3_stream_t{
                    .stream_type = CAMERA3_STREAM_OUTPUT,
                    .width = src_format.width,
                    .height = src_format.height,
                    .format = base::checked_cast<int>(src_format.format),
                    .usage = kProcessStreamUsageFlags |
                             (for_still_capture ? kStillCaptureUsageFlag : 0),
                    .max_buffers = src_max_buffers,
                    .physical_camera_id = kEmptyPhysicalCameraId,
                    .crop_rotate_scale_degrees = crop_rotate_scale_degrees,
                }),
      .max_scaling_factor = max_scaling_factor.value(),
  };
}

std::pair<StreamManipulatorHelper::CaptureContext&, base::ScopedClosureRunner>
StreamManipulatorHelper::GetCaptureContext(uint32_t frame_number) {
  CHECK(task_runner_->RunsTasksInCurrentSequence());

  auto it = capture_contexts_.find(frame_number);
  CHECK(it != capture_contexts_.end());
  CaptureContext& ctx = *it->second;
  base::ScopedClosureRunner ctx_remover(base::BindOnce(
      [](decltype(capture_contexts_)& contexts, decltype(it) it) {
        if (it->second->Done()) {
          contexts.erase(it);
        }
      },
      std::ref(capture_contexts_), std::move(it)));
  return std::make_pair(std::ref(ctx), std::move(ctx_remover));
}

void StreamManipulatorHelper::ReturnCaptureResult(
    Camera3CaptureDescriptor result, CaptureContext& capture_ctx) {
  CHECK(task_runner_->RunsTasksInCurrentSequence());
  CHECK_NE(config_.process_mode, ProcessMode::kBypass);

  const bool process_tasks_finished = [&]() {
    for (auto& [s, stream_ctx] : capture_ctx.requested_streams) {
      if (IsOutputFormatYuv(s->format) && stream_ctx.for_process &&
          stream_ctx.state != StreamState::kDone) {
        return false;
      }
    }
    return true;
  }();

  if (process_tasks_finished) {
    result.feature_metadata() = capture_ctx.feature_metadata;
  }

  if (capture_ctx.last_result_metadata_received &&
      !capture_ctx.last_result_metadata_sent &&
      !config_.result_metadata_tags_to_update.empty() &&
      process_tasks_finished) {
    CHECK_EQ(result.partial_result(), 0);
    if (!capture_ctx.result_metadata.isEmpty()) {
      result.mutable_metadata().acquire(capture_ctx.result_metadata);
    }
    result.SetPartialResult(partial_result_count_);
  }
  if (result.partial_result() == partial_result_count_) {
    capture_ctx.last_result_metadata_sent = true;
  }

  if (!result.is_empty()) {
    result_sequencer_->AddResult(std::move(result));
  }
}

void StreamManipulatorHelper::CropScaleImages(
    Camera3StreamBuffer& src_buffer,
    base::span<Camera3StreamBuffer> dst_buffers) {
  CHECK(task_runner_->RunsTasksInCurrentSequence());
  if (dst_buffers.empty()) {
    return;
  }
  CHECK(IsOutputFormatYuv(src_buffer.stream()->format));
  CHECK_EQ(src_buffer.status(), CAMERA3_BUFFER_STATUS_OK);

  const StreamFormat& src_format = GetFormat(*src_buffer.stream());
  for (auto& b : dst_buffers) {
    CHECK(IsOutputFormatYuv(b.stream()->format));
    if (src_buffer.raw_buffer().status != CAMERA3_BUFFER_STATUS_OK) {
      b.mutable_raw_buffer().status = CAMERA3_BUFFER_STATUS_ERROR;
      continue;
    }
    const StreamFormat& dst_format = GetFormat(*b.stream());
    std::optional<base::ScopedFD> fence = crop_scale_image_.Run(
        *src_buffer.buffer(), base::ScopedFD(src_buffer.take_release_fence()),
        *b.buffer(), base::ScopedFD(b.take_acquire_fence()),
        src_format.fov.GetCropWindowInto(dst_format.fov));
    if (fence.has_value()) {
      b.mutable_raw_buffer().release_fence = fence->release();
    } else {
      b.mutable_raw_buffer().status = CAMERA3_BUFFER_STATUS_ERROR;
    }
  }
}

void StreamManipulatorHelper::Reset() {
  CHECK(task_runner_->RunsTasksInCurrentSequence());

  stream_config_unsupported_ = false;

  size_t num_requesting = 0, num_pending = 0, num_processing = 0;
  for (auto& [fn, capture_ctx] : capture_contexts_) {
    for (auto& [s, stream_ctx] : capture_ctx->requested_streams) {
      switch (stream_ctx.state) {
        case StreamState::kRequesting:
          ++num_requesting;
          break;
        case StreamState::kPending:
          ++num_pending;
          break;
        case StreamState::kProcessing:
          ++num_processing;
          break;
        case StreamState::kDone:
          break;
      }
    }
  }
  if (num_requesting != 0 || num_pending != 0 || num_processing != 0) {
    LOGF(WARNING) << "StreamManipulatorHelper reset when there are still "
                  << num_requesting << " requesting, " << num_pending
                  << " pending, " << num_processing << " processing buffers";
  }
  capture_contexts_.clear();

  client_stream_to_type_.clear();
  still_process_input_stream_.reset();
  video_process_input_stream_.reset();
  blob_size_.reset();
  still_process_output_size_.reset();
  video_process_output_size_.reset();
  blob_sized_buffer_pool_.reset();
  still_process_input_pool_.reset();
  still_process_output_pool_.reset();
  video_process_input_pool_.reset();
  video_process_output_pool_.reset();
  fake_still_process_output_stream_.reset();
  fake_video_process_output_stream_.reset();
  fake_still_process_output_format_.reset();
  fake_video_process_output_format_.reset();

  still_capture_processor_->Reset();
  result_sequencer_->Reset();
}

void StreamManipulatorHelper::OnProcessTaskDone(ProcessTask& task) {
  CHECK(task_runner_->RunsTasksInCurrentSequence());

  auto [capture_ctx, capture_ctx_remover] =
      GetCaptureContext(task.frame_number());
  StreamContext& stream_ctx =
      capture_ctx.requested_streams[task.input_stream()];
  CHECK_EQ(stream_ctx.state, StreamState::kProcessing);
  stream_ctx.state = StreamState::kDone;

  if (stream_ctx.process_output->status() != CAMERA3_BUFFER_STATUS_OK) {
    for (auto& b : stream_ctx.client_yuv_buffers_to_generate) {
      SetBufferError(b);
    }
  } else {
    CropScaleImages(stream_ctx.process_output.value(),
                    stream_ctx.client_yuv_buffers_to_generate);
  }

  // Handle still capture.
  if ((task.input_stream()->usage & kStillCaptureUsageFlag) &&
      !capture_ctx.still_capture_cancelled) {
    if (stream_ctx.process_output->status() != CAMERA3_BUFFER_STATUS_OK) {
      capture_ctx.still_capture_cancelled = true;
      still_capture_processor_->CancelPendingRequest(task.frame_number());
    } else if (blob_size_ == still_process_output_size_) {
      // Pass the processed still YUV to still capture processor. The buffer is
      // moved into capture context and released until the still capture is
      // done.
      if (stream_ctx.pool_process_output.has_value()) {
        capture_ctx.pool_buffer_for_blob.swap(stream_ctx.pool_process_output);
        still_capture_processor_->QueuePendingYuvImage(
            task.frame_number(), *capture_ctx.pool_buffer_for_blob->handle(),
            base::ScopedFD(stream_ctx.process_output->take_release_fence()));
        stream_ctx.process_output.reset();
      } else {
        capture_ctx.client_buffer_for_blob.swap(stream_ctx.process_output);
        still_capture_processor_->QueuePendingYuvImage(
            task.frame_number(), *capture_ctx.client_buffer_for_blob->buffer(),
            base::ScopedFD(
                capture_ctx.client_buffer_for_blob->take_release_fence()));
      }
    } else {
      // Scale the processed still YUV before sending to still capture
      // processor.
      capture_ctx.pool_buffer_for_blob =
          blob_sized_buffer_pool_->RequestBuffer();
      CHECK(capture_ctx.pool_buffer_for_blob.has_value());
      std::optional<base::ScopedFD> fence = crop_scale_image_.Run(
          *stream_ctx.process_output->buffer(),
          base::ScopedFD(stream_ctx.process_output->take_release_fence()),
          *capture_ctx.pool_buffer_for_blob->handle(), base::ScopedFD(),
          GetFormat(*stream_ctx.process_output->stream())
              .fov.GetCropWindowInto(
                  RelativeFov(blob_size_.value(), active_array_size_)));
      if (fence.has_value()) {
        still_capture_processor_->QueuePendingYuvImage(
            task.frame_number(), *capture_ctx.pool_buffer_for_blob->handle(),
            std::move(fence.value()));
      } else {
        capture_ctx.still_capture_cancelled = true;
        still_capture_processor_->CancelPendingRequest(task.frame_number());
      }
    }
  }

  Camera3CaptureDescriptor result(
      camera3_capture_result_t{.frame_number = task.frame_number()});

  // Release or return the processing buffers.
  stream_ctx.process_input.reset();
  stream_ctx.pool_process_input.reset();
  if (stream_ctx.pool_process_output.has_value()) {
    if (!stream_ctx.process_output->WaitOnAndClearReleaseFence(
            kSyncWaitTimeoutMs)) {
      LOGF(WARNING) << "Sync wait timed out on processed output "
                    << task.frame_number() << " ("
                    << GetDebugString(task.input_stream()) << ")";
    }
    stream_ctx.pool_process_output.reset();
  } else if (stream_ctx.process_output.has_value()) {
    result.AppendOutputBuffer(std::move(stream_ctx.process_output.value()));
  }
  stream_ctx.process_output.reset();

  for (auto& b : stream_ctx.client_yuv_buffers_to_generate) {
    result.AppendOutputBuffer(std::move(b));
  }
  stream_ctx.client_yuv_buffers_to_generate.clear();

  ReturnCaptureResult(std::move(result), capture_ctx);
}

void StreamManipulatorHelper::OnStillCaptureResult(
    Camera3CaptureDescriptor result) {
  CHECK(task_runner_->RunsTasksInCurrentSequence());
  CHECK_EQ(result.num_output_buffers(), 1);

  auto [capture_ctx, capture_ctx_remover] =
      GetCaptureContext(result.frame_number());
  StreamContext& stream_ctx =
      capture_ctx.requested_streams[result.GetOutputBuffers()[0].stream()];
  CHECK_EQ(stream_ctx.state, StreamState::kProcessing);
  stream_ctx.state = StreamState::kDone;

  capture_ctx.pool_buffer_for_blob.reset();
  if (capture_ctx.client_buffer_for_blob.has_value()) {
    result.AppendOutputBuffer(
        std::move(capture_ctx.client_buffer_for_blob.value()));
    capture_ctx.client_buffer_for_blob.reset();
  }

  ReturnCaptureResult(std::move(result), capture_ctx);
}

bool StreamManipulatorHelper::CaptureContext::Done() const {
  // Check all the buffer pool handles were explicitly released, and all the
  // client buffers were returned.
  for (auto& [s, ctx] : requested_streams) {
    if (ctx.state == StreamState::kDone) {
      CHECK(!ctx.pool_process_input.has_value());
      CHECK(!ctx.pool_process_output.has_value());
      CHECK(!ctx.process_input.has_value());
      CHECK(!ctx.process_output.has_value());
      CHECK(ctx.client_yuv_buffers_to_generate.empty());
    } else {
      return false;
    }
  }
  CHECK(!pool_buffer_for_blob.has_value());
  CHECK(!client_buffer_for_blob.has_value());

  // Check result metadata was sent.
  if (last_result_metadata_received) {
    CHECK(last_result_metadata_sent);
    CHECK(result_metadata.isEmpty());
  } else {
    return false;
  }

  return true;
}

}  // namespace cros