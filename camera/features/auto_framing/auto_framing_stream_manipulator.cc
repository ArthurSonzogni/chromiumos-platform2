/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/auto_framing/auto_framing_stream_manipulator.h"

#include <sync/sync.h>

#include <algorithm>
#include <numeric>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/bind_post_task.h>
#include <base/callback_helpers.h>
#include <base/containers/contains.h>
#include <base/files/file_path.h>

#include "cros-camera/camera_metadata_utils.h"

namespace cros {

namespace {

constexpr char kMetadataDumpPath[] =
    "/run/camera/auto_framing_frame_metadata.json";

constexpr char kEnableKey[] = "enable";
constexpr char kDebugKey[] = "debug";
constexpr char kDetectorKey[] = "detector";
constexpr char kMotionModelKey[] = "motion_model";

constexpr int32_t kRequiredFrameRate = 30;
constexpr uint32_t kFramingBufferUsage = GRALLOC_USAGE_HW_CAMERA_WRITE |
                                         GRALLOC_USAGE_HW_TEXTURE |
                                         GRALLOC_USAGE_SW_READ_OFTEN;
constexpr int kSyncWaitTimeoutMs = 300;

// Find the largest stream resolution with full FOV and sufficient frame rate to
// run auto-framing on.
Size GetFullFrameResolution(const camera_metadata_t* static_info,
                            const Size& active_array_size) {
  auto stream_configs = GetRoMetadataAsSpan<int32_t>(
      static_info, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
  if (stream_configs.empty() || stream_configs.size() % 4 != 0) {
    LOGF(ERROR) << "Invalid ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS in "
                   "static metadata";
    return Size();
  }
  auto frame_durations = GetRoMetadataAsSpan<int64_t>(
      static_info, ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS);
  if (frame_durations.empty() || frame_durations.size() % 4 != 0) {
    LOGF(ERROR) << "Invalid ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS in "
                   "static metadata";
    return Size();
  }

  auto is_frame_duration_ok = [&](int32_t format, int32_t width,
                                  int32_t height) -> bool {
    constexpr int64_t kRequiredFrameDurationNs =
        (1'000'000'000LL + kRequiredFrameRate - 1) / kRequiredFrameRate;
    for (size_t i = 0; i < frame_durations.size(); i += 4) {
      if (frame_durations[i] == format && frame_durations[i + 1] == width &&
          frame_durations[i + 2] == height) {
        return frame_durations[i + 3] <= kRequiredFrameDurationNs;
      }
    }
    return false;
  };

  auto has_larger_fov = [&](const Size& lhs, const Size& rhs) -> bool {
    if (lhs.width >= rhs.width && lhs.height >= rhs.height) {
      return true;
    }
    if (lhs.width <= rhs.width && lhs.height <= rhs.height) {
      return false;
    }
    float active_aspect_ratio = static_cast<float>(active_array_size.width) /
                                static_cast<float>(active_array_size.height);
    float lhs_aspect_ratio =
        static_cast<float>(lhs.width) / static_cast<float>(lhs.height);
    float rhs_aspect_ratio =
        static_cast<float>(rhs.width) / static_cast<float>(rhs.height);
    return std::abs(lhs_aspect_ratio - active_aspect_ratio) <=
           std::abs(rhs_aspect_ratio - active_aspect_ratio);
  };

  Size max_size;
  for (size_t i = 0; i < stream_configs.size(); i += 4) {
    int32_t format = stream_configs[i];
    int32_t width = stream_configs[i + 1];
    int32_t height = stream_configs[i + 2];
    int32_t direction = stream_configs[i + 3];
    Size size(base::checked_cast<uint32_t>(width),
              base::checked_cast<uint32_t>(height));
    if ((format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
         format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) &&
        direction == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT &&
        is_frame_duration_ok(format, width, height) &&
        has_larger_fov(size, max_size)) {
      max_size = size;
    }
  }
  return max_size;
}

bool IsStreamBypassed(camera3_stream_t* stream) {
  // Ignore input/bidirectional, non-YUV, and ZSL streams.
  // TODO(kamesan): Handle blob stream.
  return stream->stream_type != CAMERA3_STREAM_OUTPUT ||
         (stream->format != HAL_PIXEL_FORMAT_YCbCr_420_888 &&
          stream->format != HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) ||
         (stream->usage & GRALLOC_USAGE_HW_CAMERA_ZSL) ==
             GRALLOC_USAGE_HW_CAMERA_ZSL;
}

base::Optional<int64_t> TryGetSensorTimestamp(Camera3CaptureDescriptor* desc) {
  base::span<const int64_t> timestamp =
      desc->GetMetadata<int64_t>(ANDROID_SENSOR_TIMESTAMP);
  return timestamp.size() == 1 ? base::make_optional(timestamp[0])
                               : base::nullopt;
}

template <class T>
std::vector<T> CopyToVector(base::span<const T> src) {
  return std::vector<T>(src.begin(), src.end());
}

Rect<float> NormalizeRect(const Rect<uint32_t>& rect, const Size& size) {
  return Rect<float>(
      static_cast<float>(rect.left) / static_cast<float>(size.width),
      static_cast<float>(rect.top) / static_cast<float>(size.height),
      static_cast<float>(rect.width) / static_cast<float>(size.width),
      static_cast<float>(rect.height) / static_cast<float>(size.height));
}

Rect<uint32_t> AdjustCropRectForScalingToTarget(const Size& size,
                                                const Rect<uint32_t>& rect,
                                                const Size& target_size) {
  // Try extend |rect| to the same aspect ratio as |target_size| within |size|.
  uint32_t x, y, w, h;
  if (rect.width * target_size.height <= target_size.width * rect.height) {
    w = static_cast<uint32_t>(static_cast<double>(rect.height) /
                                  target_size.height * target_size.width +
                              0.5);
    h = rect.height;
    if (w > size.width) {
      w = size.width;
      h = static_cast<uint32_t>(static_cast<double>(size.width) /
                                    target_size.width * target_size.height +
                                0.5);
    }
    uint32_t dx = (w - rect.width) / 2;
    x = rect.left < dx ? 0
                       : (rect.left - dx + w > size.width ? size.width - w
                                                          : rect.left - dx);
    // Prefer cropping from bottom to avoid cropping head region.
    y = rect.top;
  } else {
    w = rect.width;
    h = static_cast<uint32_t>(static_cast<double>(rect.width) /
                                  target_size.width * target_size.height +
                              0.5);
    if (h > size.height) {
      w = static_cast<uint32_t>(static_cast<double>(size.height) /
                                    target_size.height * target_size.width +
                                0.5);
      h = size.height;
    }
    uint32_t dx = (rect.width - w) / 2;
    uint32_t dy = (h - rect.height) / 2;
    x = rect.left + dx;
    y = rect.top < dy ? 0
                      : (rect.top - dy + h > size.height ? size.height - h
                                                         : rect.top - dy);
  }
  return Rect<uint32_t>(x, y, w, h);
}

std::pair<uint32_t, uint32_t> GetAspectRatio(const Size& size) {
  uint32_t g = std::gcd(size.width, size.height);
  return std::make_pair(size.width / g, size.height / g);
}

}  // namespace

//
// AutoFramingStreamManipulator implementations.
//

struct AutoFramingStreamManipulator::CaptureContext {
  std::vector<camera3_stream_buffer_t> client_buffers;
  base::Optional<CameraBufferPool::Buffer> full_frame_buffer;
  base::Optional<int64_t> timestamp;
};

AutoFramingStreamManipulator::AutoFramingStreamManipulator()
    : config_(base::FilePath(kDefaultAutoFramingConfigFile),
              base::FilePath(kOverrideAutoFramingConfigFile)),
      metadata_logger_({.dump_path = base::FilePath(kMetadataDumpPath)}),
      thread_("AutoFramingThread") {
  CHECK(thread_.Start());

  config_.SetCallback(base::BindRepeating(
      &AutoFramingStreamManipulator::OnOptionsUpdated, base::Unretained(this)));
}

AutoFramingStreamManipulator::~AutoFramingStreamManipulator() {
  thread_.PostTaskAsync(
      FROM_HERE, base::BindOnce(&AutoFramingStreamManipulator::ResetOnThread,
                                base::Unretained(this)));
  thread_.Stop();
}

bool AutoFramingStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    CaptureResultCallback result_callback) {
  bool ret;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&AutoFramingStreamManipulator::InitializeOnThread,
                     base::Unretained(this), static_info, result_callback),
      &ret);
  return ret;
}

bool AutoFramingStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  bool ret;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&AutoFramingStreamManipulator::ConfigureStreamsOnThread,
                     base::Unretained(this), stream_config),
      &ret);
  return ret;
}

bool AutoFramingStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  bool ret;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(&AutoFramingStreamManipulator::OnConfiguredStreamsOnThread,
                     base::Unretained(this), stream_config),
      &ret);
  return ret;
}

bool AutoFramingStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  // TODO(jcliang): Fill in the PTZ vendor tags.
  return true;
}

bool AutoFramingStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  bool ret;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(
          &AutoFramingStreamManipulator::ProcessCaptureRequestOnThread,
          base::Unretained(this), request),
      &ret);
  return ret;
}

bool AutoFramingStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor* result) {
  bool ret;
  thread_.PostTaskSync(
      FROM_HERE,
      base::BindOnce(
          &AutoFramingStreamManipulator::ProcessCaptureResultOnThread,
          base::Unretained(this), result),
      &ret);
  return ret;
}

bool AutoFramingStreamManipulator::Notify(camera3_notify_msg_t* msg) {
  return true;
}

bool AutoFramingStreamManipulator::Flush() {
  return true;
}

bool AutoFramingStreamManipulator::InitializeOnThread(
    const camera_metadata_t* static_info,
    CaptureResultCallback result_callback) {
  DCHECK(thread_.IsCurrentThread());

  base::span<const int32_t> active_array_size = GetRoMetadataAsSpan<int32_t>(
      static_info, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);
  DCHECK_EQ(active_array_size.size(), 4);
  VLOGF(1) << "active_array_size: (" << active_array_size[0] << ", "
           << active_array_size[1] << "), (" << active_array_size[2] << ", "
           << active_array_size[3] << ")";
  active_array_dimension_ = Size(active_array_size[2], active_array_size[3]);

  full_frame_size_ =
      GetFullFrameResolution(static_info, active_array_dimension_);
  if (full_frame_size_.is_valid()) {
    LOGF(ERROR) << "Cannot find a resolution to run auto-framing on";
    return false;
  }
  VLOGF(1) << "Full frame size for auto-framing: "
           << full_frame_size_.ToString();

  return true;
}

bool AutoFramingStreamManipulator::ConfigureStreamsOnThread(
    Camera3StreamConfiguration* stream_config) {
  DCHECK(thread_.IsCurrentThread());

  if (!options_.enable) {
    return true;
  }

  ResetOnThread();

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "Config streams from client:";
    for (auto* s : stream_config->GetStreams()) {
      VLOGF(1) << "  " << GetDebugString(s);
    }
  }

  // Filter client streams into |hal_streams| that will be requested to the HAL.
  client_streams_ = CopyToVector(stream_config->GetStreams());
  std::vector<camera3_stream_t*> hal_streams;
  const camera3_stream_t* target_output_stream = nullptr;
  for (auto* s : client_streams_) {
    if (IsStreamBypassed(s)) {
      hal_streams.push_back(s);
      continue;
    }
    // Choose the output stream of the largest resolution for matching the crop
    // window aspect ratio. Prefer taller size since extending crop windows
    // horizontally (for other outputs) looks better.
    if (!target_output_stream || s->height > target_output_stream->height ||
        (s->height == target_output_stream->height &&
         s->width > target_output_stream->width)) {
      target_output_stream = s;
    }
  }
  if (!target_output_stream) {
    LOGF(ERROR) << "No valid output stream found in stream config";
    return false;
  }
  VLOGF(1) << "Target output stream: " << GetDebugString(target_output_stream);
  auto [target_aspect_ratio_x, target_aspect_ratio_y] = GetAspectRatio(
      Size(target_output_stream->width, target_output_stream->height));

  // Create a stream to run auto-framing on.
  full_frame_stream_ = camera3_stream_t{
      .stream_type = CAMERA3_STREAM_OUTPUT,
      .width = full_frame_size_.width,
      .height = full_frame_size_.height,
      .format = HAL_PIXEL_FORMAT_YCbCr_420_888,
      .usage = kFramingBufferUsage,
      .max_buffers = 2,
  };
  hal_streams.push_back(&full_frame_stream_);

  if (!stream_config->SetStreams(hal_streams)) {
    LOGF(ERROR) << "Failed to manipulate stream config";
    return false;
  }

  if (!SetUpPipelineOnThread(target_aspect_ratio_x, target_aspect_ratio_y)) {
    return false;
  }

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "Config streams to HAL:";
    for (auto* s : stream_config->GetStreams()) {
      VLOGF(1) << "  " << GetDebugString(s)
               << (s == &full_frame_stream_ ? " (framing input)" : "");
    }
  }

  return true;
}

bool AutoFramingStreamManipulator::OnConfiguredStreamsOnThread(
    Camera3StreamConfiguration* stream_config) {
  DCHECK(thread_.IsCurrentThread());

  if (!options_.enable) {
    return true;
  }

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "Configured streams from HAL:";
    for (auto* s : stream_config->GetStreams()) {
      VLOGF(1) << "  " << GetDebugString(s);
    }
  }

  if ((full_frame_stream_.usage & kFramingBufferUsage) != kFramingBufferUsage) {
    LOGF(ERROR) << "Failed to negotiate buffer usage";
    return false;
  }
  // Allocate buffers for |full_frame_stream_|.
  CameraBufferPool::Options buffer_pool_options = {
      .width = full_frame_stream_.width,
      .height = full_frame_stream_.height,
      .format = base::checked_cast<uint32_t>(full_frame_stream_.format),
      .usage = full_frame_stream_.usage,
      .max_num_buffers =
          base::strict_cast<size_t>(full_frame_stream_.max_buffers) + 1,
  };
  full_frame_buffer_pool_ =
      std::make_unique<CameraBufferPool>(buffer_pool_options);

  if (!stream_config->SetStreams(client_streams_)) {
    LOGF(ERROR) << "Failed to recover stream config";
    return false;
  }

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "Configured streams to client:";
    for (auto* s : stream_config->GetStreams()) {
      VLOGF(1) << "  " << GetDebugString(s);
    }
  }

  return true;
}

bool AutoFramingStreamManipulator::ProcessCaptureRequestOnThread(
    Camera3CaptureDescriptor* request) {
  DCHECK(thread_.IsCurrentThread());

  if (!options_.enable) {
    return true;
  }

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, request->frame_number())
        << " Request stream buffers from client:";
    for (auto& b : request->GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream);
    }
  }

  CaptureContext* ctx = CreateCaptureContext(request->frame_number());
  if (!ctx) {
    return false;
  }

  // Separate buffers into |hal_buffers| that will be requested to the HAL, and
  // |ctx->client_buffers| that will be done by us.
  std::vector<camera3_stream_buffer_t> hal_buffers;
  for (auto& b : request->GetOutputBuffers()) {
    if (IsStreamBypassed(b.stream)) {
      hal_buffers.push_back(b);
    } else {
      ctx->client_buffers.push_back(b);
    }
  }
  // Add an output for auto-framing.
  DCHECK_NE(full_frame_buffer_pool_, nullptr);
  ctx->full_frame_buffer = full_frame_buffer_pool_->RequestBuffer();
  if (!ctx->full_frame_buffer) {
    LOGF(ERROR) << "Failed to allocate buffer for request "
                << request->frame_number();
    return false;
  }
  camera3_stream_buffer_t full_frame_buffer = {
      .stream = &full_frame_stream_,
      .buffer = ctx->full_frame_buffer->handle(),
      .status = CAMERA3_BUFFER_STATUS_OK,
      .acquire_fence = -1,
      .release_fence = -1,
  };
  hal_buffers.push_back(full_frame_buffer);

  request->SetOutputBuffers(hal_buffers);

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, request->frame_number()) << " Request stream buffers to HAL:";
    for (auto& b : request->GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream);
    }
  }

  return true;
}

bool AutoFramingStreamManipulator::ProcessCaptureResultOnThread(
    Camera3CaptureDescriptor* result) {
  DCHECK(thread_.IsCurrentThread());

  if (!options_.enable) {
    return true;
  }

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, result->frame_number()) << " Result stream buffers from HAL:";
    for (auto& b : result->GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream);
    }
  }

  CaptureContext* ctx = GetCaptureContext(result->frame_number());
  if (!ctx) {
    return false;
  }

  if (!ctx->timestamp.has_value()) {
    ctx->timestamp = TryGetSensorTimestamp(result);
  }

  camera3_stream_buffer_t full_frame_buffer = {};
  auto hal_buffers = result->GetOutputBuffers();
  auto it = std::find_if(hal_buffers.begin(), hal_buffers.end(), [&](auto& b) {
    return b.stream == &full_frame_stream_;
  });
  if (it == hal_buffers.end()) {
    // Partial result.
    if (!std::all_of(hal_buffers.begin(), hal_buffers.end(),
                     [](const camera3_stream_buffer_t& b) {
                       return IsStreamBypassed(b.stream);
                     })) {
      LOGF(ERROR) << "Unexpected output stream received in result "
                  << result->frame_number();
      return false;
    }
    return true;
  }
  full_frame_buffer = *it;

  // Now we need to convert |full_frame_buffer| into |ctx->client_buffers|.
  // Make sure we fill |result| properly when there's error.
  base::ScopedClosureRunner framing_error_handler(
      base::BindOnce(&AutoFramingStreamManipulator::HandleFramingErrorOnThread,
                     base::Unretained(this), result));

  if (full_frame_buffer.status != CAMERA3_BUFFER_STATUS_OK) {
    VLOGF(1) << "Received buffer with error in result "
             << result->frame_number();
    return false;
  }

  if (face_tracker_) {
    // Using face detector.
    if (result->feature_metadata().faces) {
      face_tracker_->OnNewFaceData(*result->feature_metadata().faces);
      faces_ = face_tracker_->GetActiveFaceRectangles();
      region_of_interest_ =
          face_tracker_->GetActiveBoundingRectangleOnActiveStream();
      frame_cropper_->OnNewRegionOfInterest(result->frame_number(),
                                            region_of_interest_);
    }
    UpdateFaceRectangleMetadataOnThread(result);
  } else {
    // Using FPP detector.
    if (!ctx->timestamp.has_value()) {
      VLOGF(1) << "Sensor timestamp not found for result "
               << result->frame_number();
      return false;
    }

    if (full_frame_buffer.release_fence != -1) {
      if (sync_wait(full_frame_buffer.release_fence, kSyncWaitTimeoutMs) != 0) {
        LOGF(ERROR) << "sync_wait() HAL buffer timed out on capture result "
                    << result->frame_number();
        return false;
      }
      close(full_frame_buffer.release_fence);
      full_frame_buffer.release_fence = -1;
    }

    if (!auto_framing_client_.ProcessFrame(*ctx->timestamp,
                                           *full_frame_buffer.buffer)) {
      LOGF(ERROR) << "Failed to process frame " << result->frame_number();
      return false;
    }

    if (!override_crop_window_) {
      base::Optional<Rect<uint32_t>> roi =
          auto_framing_client_.TakeNewRegionOfInterest();
      if (roi) {
        DCHECK_NE(frame_cropper_, nullptr);
        frame_cropper_->OnNewRegionOfInterest(
            result->frame_number(), NormalizeRect(*roi, full_frame_size_));
      }
    }
  }

  // Crop the full frame into client buffers.
  DCHECK_NE(frame_cropper_, nullptr);
  for (auto& b : ctx->client_buffers) {
    if (b.acquire_fence != -1) {
      if (sync_wait(b.acquire_fence, kSyncWaitTimeoutMs) != 0) {
        LOGF(ERROR) << "sync_wait() client buffers timed out on capture result "
                    << result->frame_number();
        return false;
      }
      close(b.acquire_fence);
      b.acquire_fence = -1;
    }
    base::Optional<Rect<float>> crop_override;
    if (options_.debug) {
      // In debug mode we draw the crop area on the full frame instead.
      crop_override = base::make_optional<Rect<float>>(0.0f, 0.0f, 1.0f, 1.0f);
    } else if (override_crop_window_) {
      Rect<uint32_t> crop_window_for_output = AdjustCropRectForScalingToTarget(
          full_frame_size_, auto_framing_client_.GetCropWindow(),
          Size(b.stream->width, b.stream->height));
      crop_override = base::make_optional<Rect<float>>(
          NormalizeRect(crop_window_for_output, full_frame_size_));
    }
    base::ScopedFD release_fence = frame_cropper_->CropBuffer(
        base::checked_cast<int>(result->frame_number()),
        *full_frame_buffer.buffer,
        base::ScopedFD(full_frame_buffer.release_fence), *b.buffer,
        crop_override);
    b.release_fence = release_fence.release();
  }

  // Done framing.
  framing_error_handler.ReplaceClosure(base::DoNothing());

  std::vector<camera3_stream_buffer_t> result_buffers;
  for (auto& b : result->GetOutputBuffers()) {
    if (b.stream != &full_frame_stream_) {
      result_buffers.push_back(b);
    }
  }
  for (auto& b : ctx->client_buffers) {
    b.status = CAMERA3_BUFFER_STATUS_OK;
    result_buffers.push_back(b);
  }
  result->SetOutputBuffers(result_buffers);

  capture_contexts_.erase(result->frame_number());

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, result->frame_number()) << " Result stream buffers to client:";
    for (auto& b : result->GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream);
    }
  }

  return true;
}

bool AutoFramingStreamManipulator::SetUpPipelineOnThread(
    uint32_t target_aspect_ratio_x, uint32_t target_aspect_ratio_y) {
  DCHECK(thread_.IsCurrentThread());

  // We only load |options_.{detector,motion_model}| once here. Later functions
  // should check |face_tracker_|, |override_crop_window_| for the selected
  // modes.
  if (options_.detector == Detector::kFace &&
      options_.motion_model == MotionModel::kLibAutoFraming) {
    LOGF(ERROR) << "Face detector cannot be paired with libautoframing";
    return false;
  }
  switch (options_.detector) {
    case Detector::kFace: {
      face_tracker_ = std::make_unique<FaceTracker>(FaceTracker::Options{
          .active_array_dimension = active_array_dimension_,
          .active_stream_dimension = full_frame_size_});
      break;
    }
    case Detector::kFacePersonPose: {
      if (!auto_framing_client_.SetUp(AutoFramingClient::Options{
              .input_size = full_frame_size_,
              .frame_rate = static_cast<double>(kRequiredFrameRate),
              .target_aspect_ratio_x = target_aspect_ratio_x,
              .target_aspect_ratio_y = target_aspect_ratio_y,
          })) {
        return false;
      }
      break;
    }
  }
  override_crop_window_ = options_.motion_model == MotionModel::kLibAutoFraming;

  frame_cropper_ = std::make_unique<FrameCropper>(
      FrameCropper::Options{.input_size = full_frame_size_},
      thread_.task_runner());

  return true;
}

void AutoFramingStreamManipulator::UpdateFaceRectangleMetadataOnThread(
    Camera3CaptureDescriptor* result) {
  DCHECK(thread_.IsCurrentThread());

  if (!result->has_metadata()) {
    return;
  }

  std::vector<Rect<float>> face_rectangles;
  if (options_.debug) {
    // Show the detected faces, aggregated region of interest and the active
    // crop region in debug mode.
    face_rectangles = faces_;
    face_rectangles.push_back(region_of_interest_);
    Rect<float> active_crop_region = frame_cropper_->GetActiveCropRegion();
    face_rectangles.push_back(active_crop_region);
    if (!result->UpdateMetadata<uint8_t>(
            ANDROID_STATISTICS_FACE_DETECT_MODE,
            (uint8_t[]){ANDROID_STATISTICS_FACE_DETECT_MODE_SIMPLE})) {
      LOGF(ERROR) << "Cannot set ANDROID_STATISTICS_FACE_DETECT_MODE";
    }
  } else {
    // By default translate the face rectangles in the result metadata to the
    // crop coordinate space.
    base::span<const int32_t> raw_face_rectangles =
        result->GetMetadata<int32_t>(ANDROID_STATISTICS_FACE_RECTANGLES);
    if (raw_face_rectangles.empty()) {
      return;
    }
    for (size_t i = 0; i < raw_face_rectangles.size(); i += 4) {
      const int* rect_bound = &raw_face_rectangles[i];
      face_rectangles.push_back(Rect<float>(
          static_cast<float>(rect_bound[0]) / active_array_dimension_.width,
          static_cast<float>(rect_bound[1]) / active_array_dimension_.height,
          static_cast<float>(rect_bound[2] - rect_bound[0]) /
              active_array_dimension_.width,
          static_cast<float>(rect_bound[3] - rect_bound[1]) /
              active_array_dimension_.height));
    }
    frame_cropper_->ConvertToCropSpace(&face_rectangles);
  }
  std::vector<int32_t> face_coordinates;
  for (const auto& f : face_rectangles) {
    face_coordinates.push_back(f.left * active_array_dimension_.width);
    face_coordinates.push_back(f.top * active_array_dimension_.height);
    face_coordinates.push_back(f.right() * active_array_dimension_.width);
    face_coordinates.push_back(f.bottom() * active_array_dimension_.height);
  }
  if (!result->UpdateMetadata<int32_t>(ANDROID_STATISTICS_FACE_RECTANGLES,
                                       face_coordinates)) {
    LOGF(ERROR) << "Cannot set ANDROID_STATISTICS_FACE_RECTANGLES";
  }
}

void AutoFramingStreamManipulator::HandleFramingErrorOnThread(
    Camera3CaptureDescriptor* result) {
  DCHECK(thread_.IsCurrentThread());

  CaptureContext* ctx = GetCaptureContext(result->frame_number());
  if (!ctx) {
    return;
  }

  std::vector<camera3_stream_buffer_t> result_buffers;
  for (auto& b : result->GetOutputBuffers()) {
    if (b.stream != &full_frame_stream_) {
      result_buffers.push_back(b);
    }
  }
  for (auto& b : ctx->client_buffers) {
    b.status = CAMERA3_BUFFER_STATUS_ERROR;
    result_buffers.push_back(b);
  }
  result->SetOutputBuffers(result_buffers);

  capture_contexts_.erase(result->frame_number());
}

void AutoFramingStreamManipulator::ResetOnThread() {
  DCHECK(thread_.IsCurrentThread());

  auto_framing_client_.TearDown();
  face_tracker_.reset();
  frame_cropper_.reset();

  client_streams_.clear();
  full_frame_stream_ = {};
  capture_contexts_.clear();
  full_frame_buffer_pool_.reset();
}

void AutoFramingStreamManipulator::UpdateOptionsOnThread(
    const base::Value& json_values) {
  DCHECK(thread_.IsCurrentThread());

  int detector, motion_model;
  if (LoadIfExist(json_values, kDetectorKey, &detector)) {
    options_.detector = static_cast<Detector>(detector);
  }
  if (LoadIfExist(json_values, kMotionModelKey, &motion_model)) {
    options_.motion_model = static_cast<MotionModel>(motion_model);
  }
  LoadIfExist(json_values, kEnableKey, &options_.enable);
  LoadIfExist(json_values, kDebugKey, &options_.debug);

  VLOGF(1) << "AutoFramingStreamManipulator options:"
           << " detector=" << static_cast<int>(options_.detector)
           << " motion_model=" << static_cast<int>(options_.motion_model)
           << " enable=" << options_.enable << " debug=" << options_.debug;

  if (face_tracker_) {
    face_tracker_->OnOptionsUpdated(json_values);
  }
  if (frame_cropper_) {
    frame_cropper_->OnOptionsUpdated(json_values);
  }
}

void AutoFramingStreamManipulator::OnOptionsUpdated(
    const base::Value& json_values) {
  thread_.PostTaskAsync(
      FROM_HERE,
      base::BindOnce(&AutoFramingStreamManipulator::UpdateOptionsOnThread,
                     base::Unretained(this), std::cref(json_values)));
}

AutoFramingStreamManipulator::CaptureContext*
AutoFramingStreamManipulator::CreateCaptureContext(uint32_t frame_number) {
  CHECK(!base::Contains(capture_contexts_, frame_number));
  auto [it, is_inserted] = capture_contexts_.insert(
      std::make_pair(frame_number, std::make_unique<CaptureContext>()));
  if (!is_inserted) {
    LOGF(ERROR) << "Multiple captures with same frame number " << frame_number;
    return nullptr;
  }
  return it->second.get();
}

AutoFramingStreamManipulator::CaptureContext*
AutoFramingStreamManipulator::GetCaptureContext(uint32_t frame_number) const {
  auto it = capture_contexts_.find(frame_number);
  if (it == capture_contexts_.end()) {
    LOGF(ERROR) << "Cannot find capture context with frame number "
                << frame_number;
    return nullptr;
  }
  return it->second.get();
}

}  // namespace cros
