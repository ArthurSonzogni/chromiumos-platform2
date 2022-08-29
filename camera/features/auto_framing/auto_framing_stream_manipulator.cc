/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/auto_framing/auto_framing_stream_manipulator.h"

#include <sync/sync.h>

#include <algorithm>
#include <numeric>
#include <optional>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/containers/contains.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/task/bind_post_task.h>
#include <base/strings/string_number_conversions.h>
#include <sys/types.h>

#include "cros-camera/camera_metadata_utils.h"
#include "gpu/egl/egl_context.h"
#include "gpu/egl/egl_fence.h"
#include "gpu/shared_image.h"

namespace cros {

namespace {

constexpr char kMetadataDumpPath[] =
    "/run/camera/auto_framing_frame_metadata.json";

constexpr char kEnableKey[] = "enable";
constexpr char kDebugKey[] = "debug";
constexpr char kMaxFullWidthKey[] = "max_video_width";
constexpr char kMaxFullHeightKey[] = "max_video_height";
constexpr char kOutputFilterModeKey[] = "output_filter_mode";
constexpr char kDetectionRateKey[] = "detection_rate";
constexpr char kEnableDelayKey[] = "enable_delay";
constexpr char kDisableDelayKey[] = "disable_delay";

constexpr int32_t kRequiredVideoFrameRate = 30;
constexpr uint32_t kFullFrameBufferUsage = GRALLOC_USAGE_HW_CAMERA_WRITE |
                                           GRALLOC_USAGE_HW_TEXTURE |
                                           GRALLOC_USAGE_SW_READ_OFTEN;
#if USE_IPU6 || USE_IPU6EP
// On Intel platforms, the GRALLOC_USAGE_PRIVATE_1 usage bit tells the camera
// HAL to process the stream using the still pipe for higher quality output.
constexpr uint32_t kStillYuvBufferUsage =
    kFullFrameBufferUsage | GRALLOC_USAGE_PRIVATE_1;
#else
constexpr uint32_t kStillYuvBufferUsage = kFullFrameBufferUsage;
#endif  // USE_IPU6 || USE_IPU6EP
constexpr int kSyncWaitTimeoutMs = 300;

// Find the largest (video, still) stream resolutions with full FOV.
std::pair<Size, Size> GetFullFrameResolutions(
    const camera_metadata_t* static_info,
    const Size& active_array_size,
    std::optional<uint32_t> max_video_width,
    std::optional<uint32_t> max_video_height) {
  auto stream_configs = GetRoMetadataAsSpan<int32_t>(
      static_info, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS);
  if (stream_configs.empty() || stream_configs.size() % 4 != 0) {
    LOGF(ERROR) << "Invalid ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS in "
                   "static metadata";
    return {};
  }
  auto frame_durations = GetRoMetadataAsSpan<int64_t>(
      static_info, ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS);
  if (frame_durations.empty() || frame_durations.size() % 4 != 0) {
    LOGF(ERROR) << "Invalid ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS in "
                   "static metadata";
    return {};
  }

  auto is_frame_duration_ok_for_video = [&](int32_t format, int32_t width,
                                            int32_t height) -> bool {
    constexpr int64_t kRequiredFrameDurationNs =
        (1'000'000'000LL + kRequiredVideoFrameRate - 1) /
        kRequiredVideoFrameRate;
    for (size_t i = 0; i < frame_durations.size(); i += 4) {
      if (frame_durations[i] == format && frame_durations[i + 1] == width &&
          frame_durations[i + 2] == height) {
        return frame_durations[i + 3] <= kRequiredFrameDurationNs;
      }
    }
    return false;
  };

  auto is_larger_or_closer_to_native_aspect_ratio =
      [&](const Size& lhs, const Size& rhs) -> bool {
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

  Size max_video_size, max_still_size;
  for (size_t i = 0; i < stream_configs.size(); i += 4) {
    int32_t format = stream_configs[i];
    int32_t width = stream_configs[i + 1];
    int32_t height = stream_configs[i + 2];
    int32_t direction = stream_configs[i + 3];
    Size size(base::checked_cast<uint32_t>(width),
              base::checked_cast<uint32_t>(height));
    if ((format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
         format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) &&
        direction == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT) {
      if (is_frame_duration_ok_for_video(format, width, height) &&
          is_larger_or_closer_to_native_aspect_ratio(size, max_video_size) &&
          (!max_video_width.has_value() || size.width <= *max_video_width) &&
          (!max_video_height.has_value() || size.height <= *max_video_height)) {
        max_video_size = size;
      }
      if (is_larger_or_closer_to_native_aspect_ratio(size, max_still_size)) {
        max_still_size = size;
      }
    }
  }
  return std::make_pair(max_video_size, max_still_size);
}

bool IsStreamBypassed(camera3_stream_t* stream) {
  return stream->stream_type == CAMERA3_STREAM_INPUT ||
         (stream->format != HAL_PIXEL_FORMAT_YCbCr_420_888 &&
          stream->format != HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED &&
          stream->format != HAL_PIXEL_FORMAT_BLOB) ||
         (stream->usage & GRALLOC_USAGE_HW_CAMERA_ZSL) ==
             GRALLOC_USAGE_HW_CAMERA_ZSL;
}

std::optional<int64_t> TryGetSensorTimestamp(Camera3CaptureDescriptor* desc) {
  base::span<const int64_t> timestamp =
      desc->GetMetadata<int64_t>(ANDROID_SENSOR_TIMESTAMP);
  return timestamp.size() == 1 ? std::make_optional(timestamp[0])
                               : std::nullopt;
}

template <class T>
std::vector<T> CopyToVector(base::span<const T> src) {
  return std::vector<T>(src.begin(), src.end());
}

Rect<float> AdjustCropRectToTargetAspectRatio(const Rect<float>& rect,
                                              float target_aspect_ratio) {
  const float aspect_ratio = rect.width / rect.height;
  float x, y, w, h;
  if (aspect_ratio <= target_aspect_ratio) {
    w = rect.height * target_aspect_ratio;
    h = rect.height;
    if (w > 1.0f) {
      w = 1.0f;
      h = 1.0f / target_aspect_ratio;
    }
    const float dx = (w - rect.width) * 0.5f;
    x = std::clamp(rect.left - dx, 0.0f, 1.0f - w);
    // Prefer cropping from bottom to avoid cropping head region.
    y = rect.top;
  } else {
    w = rect.width;
    h = rect.width / target_aspect_ratio;
    if (h > 1.0f) {
      w = target_aspect_ratio;
      h = 1.0f;
    }
    const float dx = (rect.width - w) * 0.5f;
    const float dy = (h - rect.height) * 0.5f;
    x = rect.left + dx;
    y = std::clamp(rect.top - dy, 0.0f, 1.0f - h);
  }
  return Rect<float>(x, y, w, h);
}

// Converts |rect| to the simulated active array region corresponding to the
// |crop_region| seen by the client.  |rect| and |crop_region| coordinates are
// relative to the active array size.
Rect<float> ConvertToCropSpace(const Rect<float>& rect,
                               const Rect<float>& crop_region) {
  const float active_region_dim =
      std::max(crop_region.width, crop_region.height);
  const float active_region_x =
      crop_region.left + (crop_region.width - active_region_dim) * 0.5f;
  const float active_region_y =
      crop_region.top + (crop_region.height - active_region_dim) * 0.5f;
  const float mapped_rect_x0 =
      std::clamp((rect.left - active_region_x) / active_region_dim, 0.0f, 1.0f);
  const float mapped_rect_y0 =
      std::clamp((rect.top - active_region_y) / active_region_dim, 0.0f, 1.0f);
  const float mapped_rect_x1 = std::clamp(
      (rect.right() - active_region_x) / active_region_dim, 0.0f, 1.0f);
  const float mapped_rect_y1 = std::clamp(
      (rect.bottom() - active_region_y) / active_region_dim, 0.0f, 1.0f);
  return Rect<float>(mapped_rect_x0, mapped_rect_y0,
                     mapped_rect_x1 - mapped_rect_x0,
                     mapped_rect_y1 - mapped_rect_y0);
}

Rect<float> ConvertToParentSpace(const Rect<float>& rect,
                                 const Rect<float>& crop_region) {
  return Rect<float>(rect.left * crop_region.width + crop_region.left,
                     rect.top * crop_region.height + crop_region.top,
                     rect.width * crop_region.width,
                     rect.height * crop_region.height);
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
  uint32_t num_pending_buffers = 0;
  bool metadata_received = false;
  bool has_pending_blob = false;
  std::vector<camera3_stream_buffer_t> client_buffers;
  std::optional<CameraBufferPool::Buffer> full_frame_buffer;
  std::optional<CameraBufferPool::Buffer> still_yuv_buffer;
  std::optional<CameraBufferPool::Buffer> cropped_still_yuv_buffer;
  std::optional<int64_t> timestamp;
  std::optional<std::pair<State, State>> state_transition;
  std::optional<Rect<float>> crop_region;
};

AutoFramingStreamManipulator::AutoFramingStreamManipulator(
    RuntimeOptions* runtime_options,
    base::FilePath config_file_path,
    std::unique_ptr<StillCaptureProcessor> still_capture_processor,
    std::optional<Options> options_override_for_testing)
    : config_(ReloadableConfigFile::Options{
          .default_config_file_path = std::move(config_file_path),
          .override_config_file_path =
              base::FilePath(kOverrideAutoFramingConfigFile)}),
      runtime_options_(runtime_options),
      still_capture_processor_(std::move(still_capture_processor)),
      metadata_logger_({.dump_path = base::FilePath(kMetadataDumpPath)}),
      thread_("AutoFramingThread") {
  DCHECK_NE(runtime_options_, nullptr);
  CHECK(thread_.Start());

  if (options_override_for_testing) {
    options_ = *options_override_for_testing;
  } else {
    if (!config_.IsValid()) {
      LOGF(ERROR) << "Cannot load valid config; turn off feature by default";
      options_.enable = false;
    }
    config_.SetCallback(
        base::BindRepeating(&AutoFramingStreamManipulator::OnOptionsUpdated,
                            base::Unretained(this)));
  }
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

  result_callback_ = result_callback;

  std::optional<int32_t> partial_result_count =
      GetRoMetadata<int32_t>(static_info, ANDROID_REQUEST_PARTIAL_RESULT_COUNT);
  partial_result_count_ = partial_result_count.value_or(1);

  base::span<const int32_t> active_array_size = GetRoMetadataAsSpan<int32_t>(
      static_info, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);
  DCHECK_EQ(active_array_size.size(), 4);
  VLOGF(1) << "active_array_size: (" << active_array_size[0] << ", "
           << active_array_size[1] << "), (" << active_array_size[2] << ", "
           << active_array_size[3] << ")";
  active_array_dimension_ = Size(active_array_size[2], active_array_size[3]);
  if (!active_array_dimension_.is_valid()) {
    LOGF(ERROR) << "Invalid active array size: "
                << active_array_dimension_.ToString();
    return false;
  }

  std::tie(full_frame_size_, still_size_) = GetFullFrameResolutions(
      static_info, active_array_dimension_, options_.max_video_width,
      options_.max_video_height);
  if (!full_frame_size_.is_valid() || !still_size_.is_valid()) {
    LOGF(ERROR) << "Cannot find suitable full video/still frame resolutions";
    return false;
  }
  VLOGF(1) << "Full frame sizes: video=" << full_frame_size_.ToString()
           << ", still=" << still_size_.ToString();

  full_frame_crop_ = NormalizeRect(
      GetCenteringFullCrop(active_array_dimension_, full_frame_size_.width,
                           full_frame_size_.height),
      active_array_dimension_);

  return true;
}

bool AutoFramingStreamManipulator::ConfigureStreamsOnThread(
    Camera3StreamConfiguration* stream_config) {
  DCHECK(thread_.IsCurrentThread());

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
  Size target_size;
  for (auto* s : client_streams_) {
    if (IsStreamBypassed(s)) {
      hal_streams.push_back(s);
      continue;
    }
    if (s->format == HAL_PIXEL_FORMAT_BLOB) {
      // Process the BLOB stream inplace.
      hal_streams.push_back(s);
      still_capture_processor_->Initialize(
          s, base::BindPostTask(
                 thread_.task_runner(),
                 base::BindRepeating(&AutoFramingStreamManipulator::
                                         ReturnStillCaptureResultOnThread,
                                     base::Unretained(this))));
      blob_stream_ = s;
      // Maybe create a still YUV stream for generating higher quality BLOB.
      if (still_size_.width > full_frame_size_.width ||
          still_size_.height > full_frame_size_.height) {
        DCHECK(!still_yuv_stream_);
        still_yuv_stream_ = std::make_unique<camera3_stream_t>(camera3_stream_t{
            .stream_type = CAMERA3_STREAM_OUTPUT,
            .width = still_size_.width,
            .height = still_size_.height,
            .format = HAL_PIXEL_FORMAT_YCbCr_420_888,
            .usage = kStillYuvBufferUsage,
        });
        hal_streams.push_back(still_yuv_stream_.get());
        yuv_stream_for_blob_ = still_yuv_stream_.get();
      }
    }
    // Choose the output stream of the largest resolution for matching the crop
    // window aspect ratio. Prefer taller size since extending crop windows
    // horizontally (for other outputs) looks better.
    if (!target_size.is_valid() || s->height > target_size.height ||
        (s->height == target_size.height && s->width > target_size.width)) {
      target_size = Size(s->width, s->height);
    }
  }
  if (!target_size.is_valid()) {
    LOGF(ERROR) << "No valid output stream found in stream config";
    return false;
  }
  auto [target_aspect_ratio_x, target_aspect_ratio_y] =
      GetAspectRatio(target_size);
  VLOGF(1) << "Target output aspect ratio: " << target_aspect_ratio_x << ":"
           << target_aspect_ratio_y;

  // Create a stream to run auto-framing on.
  full_frame_stream_ = camera3_stream_t{
      .stream_type = CAMERA3_STREAM_OUTPUT,
      .width = full_frame_size_.width,
      .height = full_frame_size_.height,
      .format = HAL_PIXEL_FORMAT_YCbCr_420_888,
      .usage = kFullFrameBufferUsage,
  };
  hal_streams.push_back(&full_frame_stream_);
  if (!yuv_stream_for_blob_) {
    yuv_stream_for_blob_ = &full_frame_stream_;
  }

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

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "Configured streams from HAL:";
    for (auto* s : stream_config->GetStreams()) {
      VLOGF(1) << "  " << GetDebugString(s);
    }
  }

  if ((full_frame_stream_.usage & kFullFrameBufferUsage) !=
      kFullFrameBufferUsage) {
    LOGF(ERROR) << "Failed to negotiate buffer usage on full frame stream";
    return false;
  }
  full_frame_buffer_pool_ =
      std::make_unique<CameraBufferPool>(CameraBufferPool::Options{
          .width = full_frame_stream_.width,
          .height = full_frame_stream_.height,
          .format = base::checked_cast<uint32_t>(full_frame_stream_.format),
          .usage = full_frame_stream_.usage,
          .max_num_buffers =
              base::strict_cast<size_t>(full_frame_stream_.max_buffers) + 1,
      });

  if (blob_stream_) {
    cropped_still_yuv_buffer_pool_ =
        std::make_unique<CameraBufferPool>(CameraBufferPool::Options{
            .width = blob_stream_->width,
            .height = blob_stream_->height,
            .format = HAL_PIXEL_FORMAT_YCBCR_420_888,
            .usage = GRALLOC_USAGE_HW_TEXTURE,
            .max_num_buffers =
                base::strict_cast<size_t>(blob_stream_->max_buffers) + 1,
        });
  }

  if (still_yuv_stream_) {
    if ((still_yuv_stream_->usage & kStillYuvBufferUsage) !=
        kStillYuvBufferUsage) {
      LOGF(ERROR) << "Failed to negotiate buffer usage on still YUV stream";
      return false;
    }
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
    if (!IsStreamBypassed(s) && s->format != HAL_PIXEL_FORMAT_BLOB) {
      s->max_buffers = full_frame_stream_.max_buffers;
    }
  }

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

bool AutoFramingStreamManipulator::GetEnabled() const {
  // Use option in config file first.
  // TODO(pihsun): Handle multi people mode.
  // TODO(pihsun): ReloadableConfigFile merges new config to old config, so
  // this won't be "unset" after set, which will be confusing for developers.
  return options_.enable.value_or(runtime_options_->auto_framing_state !=
                                  mojom::CameraAutoFramingState::OFF);
}

bool AutoFramingStreamManipulator::ProcessCaptureRequestOnThread(
    Camera3CaptureDescriptor* request) {
  DCHECK(thread_.IsCurrentThread());

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, request->frame_number())
        << "Request stream buffers from client:";
    for (auto& b : request->GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream);
    }
  }

  // Ignore reprocessing requests.
  if (request->GetInputBuffer()) {
    return true;
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
    } else if (b.stream == blob_stream_) {
      hal_buffers.push_back(b);
      ctx->has_pending_blob = true;
      const camera3_capture_request_t* locked_request =
          request->LockForRequest();
      still_capture_processor_->QueuePendingOutputBuffer(
          request->frame_number(), b, locked_request->settings);
      request->Unlock();
    } else {
      ctx->client_buffers.push_back(b);
    }
  }

  // Add full frame output.
  if (!ctx->client_buffers.empty() ||
      (ctx->has_pending_blob && !still_yuv_stream_)) {
    DCHECK_NE(full_frame_buffer_pool_, nullptr);
    ctx->full_frame_buffer = full_frame_buffer_pool_->RequestBuffer();
    if (!ctx->full_frame_buffer) {
      LOGF(ERROR) << "Failed to allocate full frame buffer for request "
                  << request->frame_number();
      return false;
    }
    hal_buffers.push_back(camera3_stream_buffer_t{
        .stream = &full_frame_stream_,
        .buffer = ctx->full_frame_buffer->handle(),
        .status = CAMERA3_BUFFER_STATUS_OK,
        .acquire_fence = -1,
        .release_fence = -1,
    });
  }

  // Add still YUV output.
  if (ctx->has_pending_blob && still_yuv_stream_) {
    DCHECK_NE(still_yuv_buffer_pool_, nullptr);
    ctx->still_yuv_buffer = still_yuv_buffer_pool_->RequestBuffer();
    if (!ctx->still_yuv_buffer) {
      LOGF(ERROR) << "Failed to allocate still YUV buffer for request "
                  << request->frame_number();
      return false;
    }
    hal_buffers.push_back(camera3_stream_buffer_t{
        .stream = still_yuv_stream_.get(),
        .buffer = ctx->still_yuv_buffer->handle(),
        .status = CAMERA3_BUFFER_STATUS_OK,
        .acquire_fence = -1,
        .release_fence = -1,
    });
  }

  request->SetOutputBuffers(hal_buffers);
  ctx->num_pending_buffers = request->num_output_buffers();

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, request->frame_number()) << "Request stream buffers to HAL:";
    for (auto& b : request->GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream);
    }
  }

  return true;
}

bool AutoFramingStreamManipulator::ProcessCaptureResultOnThread(
    Camera3CaptureDescriptor* result) {
  DCHECK(thread_.IsCurrentThread());

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, result->frame_number()) << "Result stream buffers from HAL:";
    for (auto& b : result->GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream);
    }
  }

  CaptureContext* ctx = GetCaptureContext(result->frame_number());
  if (!ctx) {
    return true;
  }
  CHECK_GE(ctx->num_pending_buffers, result->num_output_buffers());
  ctx->num_pending_buffers -= result->num_output_buffers();
  ctx->metadata_received |= result->partial_result() == partial_result_count_;

  base::ScopedClosureRunner ctx_deleter;
  if (ctx->num_pending_buffers == 0 && ctx->metadata_received &&
      !ctx->has_pending_blob) {
    ctx_deleter.ReplaceClosure(
        base::BindOnce(&AutoFramingStreamManipulator::RemoveCaptureContext,
                       base::Unretained(this), result->frame_number()));
  }

  // Update state when the first result is received for each frame number.
  if (!ctx->state_transition) {
    ctx->state_transition = StateTransitionOnThread();
  }

  // Update face metadata using the last framing information.
  UpdateFaceRectangleMetadataOnThread(result);

  if (!ctx->timestamp.has_value()) {
    ctx->timestamp = TryGetSensorTimestamp(result);
    // Handle out-of-order timestamps by adding an offset.
    if (ctx->timestamp.has_value()) {
      if (*ctx->timestamp + timestamp_offset_ <= last_timestamp_) {
        timestamp_offset_ = last_timestamp_ + 1 - *ctx->timestamp;
        LOGF(WARNING) << "Found out-of-order timestamp; compensate by "
                         "increasing offset to "
                      << timestamp_offset_;
      }
      *ctx->timestamp += timestamp_offset_;
      last_timestamp_ = *ctx->timestamp;
    }
  }

  std::optional<camera3_stream_buffer_t> full_frame_buffer;
  std::optional<camera3_stream_buffer_t> still_yuv_buffer;
  std::optional<camera3_stream_buffer_t> blob_buffer;
  for (auto& b : result->GetOutputBuffers()) {
    if (b.stream == &full_frame_stream_) {
      full_frame_buffer = b;
    } else if (still_yuv_stream_ && b.stream == still_yuv_stream_.get()) {
      still_yuv_buffer = b;
    } else if (blob_stream_ && b.stream == &*blob_stream_) {
      blob_buffer = b;
    }
  }

  std::vector<camera3_stream_buffer_t> result_buffers;
  for (auto& b : result->GetOutputBuffers()) {
    if (IsStreamBypassed(b.stream)) {
      result_buffers.push_back(b);
    }
  }
  if (full_frame_buffer) {
    const bool ok = ProcessFullFrameOnThread(ctx, &*full_frame_buffer,
                                             result->frame_number());
    for (auto& b : ctx->client_buffers) {
      b.status = ok ? CAMERA3_BUFFER_STATUS_OK : CAMERA3_BUFFER_STATUS_ERROR;
      result_buffers.push_back(b);
    }
  }
  if (still_yuv_buffer) {
    // TODO(kamesan): Fail the capture result if processing fails.
    CHECK(ProcessStillYuvOnThread(ctx, &*still_yuv_buffer,
                                  result->frame_number()));
  }
  if (blob_buffer) {
    still_capture_processor_->QueuePendingAppsSegments(
        result->frame_number(), *blob_buffer->buffer,
        base::ScopedFD(blob_buffer->release_fence));
    blob_buffer->release_fence = -1;
  }
  result->SetOutputBuffers(result_buffers);

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, result->frame_number()) << "Result stream buffers to client:";
    for (auto& b : result->GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream);
    }
  }

  return true;
}

bool AutoFramingStreamManipulator::ProcessFullFrameOnThread(
    CaptureContext* ctx,
    camera3_stream_buffer_t* full_frame_buffer,
    uint32_t frame_number) {
  DCHECK(thread_.IsCurrentThread());
  DCHECK_NE(ctx, nullptr);
  DCHECK_NE(full_frame_buffer, nullptr);

  if (full_frame_buffer->status != CAMERA3_BUFFER_STATUS_OK) {
    VLOGF(1) << "Received full frame buffer with error in result "
             << frame_number;
    return false;
  }

  if (!ctx->timestamp.has_value()) {
    VLOGF(1) << "Sensor timestamp not found for result " << frame_number
             << "; using last timestamp plus 1";
    ctx->timestamp = last_timestamp_ + 1;
    last_timestamp_ = *ctx->timestamp;
  }

  if (full_frame_buffer->release_fence != -1) {
    if (sync_wait(full_frame_buffer->release_fence, kSyncWaitTimeoutMs) != 0) {
      LOGF(ERROR) << "sync_wait() HAL buffer timed out on capture result "
                  << frame_number;
      return false;
    }
    close(full_frame_buffer->release_fence);
    full_frame_buffer->release_fence = -1;
  }

  if (ctx->state_transition->first != State::kOff &&
      ctx->state_transition->second == State::kOff) {
    if (!auto_framing_client_.ResetCropWindow(*ctx->timestamp)) {
      LOGF(ERROR) << "Failed to reset crop window at result " << frame_number;
      return false;
    }
  }
  if (ctx->state_transition->first != State::kOn &&
      ctx->state_transition->second == State::kOn) {
    auto_framing_client_.ResetDetectionTimer();
  }
  if (!auto_framing_client_.ProcessFrame(
          *ctx->timestamp, ctx->state_transition->second == State::kOn
                               ? *full_frame_buffer->buffer
                               : nullptr)) {
    LOGF(ERROR) << "Failed to process frame " << frame_number;
    return false;
  }

  std::optional<Rect<float>> roi =
      auto_framing_client_.TakeNewRegionOfInterest();
  if (roi) {
    region_of_interest_ = *roi;
  }

  // Crop the full frame into client buffers.
  ctx->crop_region = auto_framing_client_.GetCropWindow(*ctx->timestamp);
  active_crop_region_ = *ctx->crop_region;
  for (auto& b : ctx->client_buffers) {
    Rect<float> adjusted_crop_region;
    if (options_.debug) {
      // In debug mode we draw the crop area on the full frame instead.
      adjusted_crop_region =
          NormalizeRect(GetCenteringFullCrop(full_frame_size_, b.stream->width,
                                             b.stream->height),
                        full_frame_size_);
    } else {
      adjusted_crop_region = AdjustCropRectToTargetAspectRatio(
          *ctx->crop_region,
          static_cast<float>(full_frame_size_.height * b.stream->width) /
              static_cast<float>(full_frame_size_.width * b.stream->height));
    }
    base::ScopedFD release_fence = CropBufferOnThread(
        *full_frame_buffer->buffer, base::ScopedFD(), *b.buffer,
        base::ScopedFD(b.acquire_fence), adjusted_crop_region);
    b.acquire_fence = -1;
    b.release_fence = release_fence.release();
  }

  // Crop the full frame into intermediate buffer for BLOB if an additional
  // still YUV stream is not used.
  if (ctx->has_pending_blob && !still_yuv_stream_) {
    ctx->cropped_still_yuv_buffer =
        cropped_still_yuv_buffer_pool_->RequestBuffer();
    if (!ctx->cropped_still_yuv_buffer) {
      LOGF(ERROR) << "Failed to allocate cropped still YUV buffer on result "
                  << frame_number;
      return false;
    }
    const Rect<float> adjusted_crop_region = AdjustCropRectToTargetAspectRatio(
        *ctx->crop_region,
        static_cast<float>(full_frame_size_.height * blob_stream_->width) /
            static_cast<float>(full_frame_size_.width * blob_stream_->height));
    base::ScopedFD release_fence =
        CropBufferOnThread(*full_frame_buffer->buffer, base::ScopedFD(),
                           *ctx->cropped_still_yuv_buffer->handle(),
                           base::ScopedFD(), adjusted_crop_region);
    still_capture_processor_->QueuePendingYuvImage(
        frame_number, *ctx->cropped_still_yuv_buffer->handle(),
        std::move(release_fence));
  }

  ctx->full_frame_buffer = std::nullopt;
  return true;
}

bool AutoFramingStreamManipulator::ProcessStillYuvOnThread(
    CaptureContext* ctx,
    camera3_stream_buffer_t* still_yuv_buffer,
    uint32_t frame_number) {
  DCHECK(thread_.IsCurrentThread());
  DCHECK_NE(ctx, nullptr);
  DCHECK_NE(still_yuv_buffer, nullptr);

  if (still_yuv_buffer->status != CAMERA3_BUFFER_STATUS_OK) {
    VLOGF(1) << "Received still YUV buffer with error in result "
             << frame_number;
    return false;
  }

  // Use the latest crop region if we don't process full frame in this request
  // (e.g. BLOB only requests).
  if (!ctx->crop_region) {
    ctx->crop_region = active_crop_region_;
  }

  ctx->cropped_still_yuv_buffer =
      cropped_still_yuv_buffer_pool_->RequestBuffer();
  if (!ctx->cropped_still_yuv_buffer) {
    LOGF(ERROR) << "Failed to allocate cropped still YUV buffer on result "
                << frame_number;
    return false;
  }
  const Rect<float> adjusted_crop_region = AdjustCropRectToTargetAspectRatio(
      *ctx->crop_region,
      static_cast<float>(full_frame_size_.height * blob_stream_->width) /
          static_cast<float>(full_frame_size_.width * blob_stream_->height));
  base::ScopedFD release_fence =
      CropBufferOnThread(*still_yuv_buffer->buffer,
                         base::ScopedFD(still_yuv_buffer->release_fence),
                         *ctx->cropped_still_yuv_buffer->handle(),
                         base::ScopedFD(), adjusted_crop_region);
  still_yuv_buffer->release_fence = -1;
  still_capture_processor_->QueuePendingYuvImage(
      frame_number, *ctx->cropped_still_yuv_buffer->handle(),
      std::move(release_fence));

  ctx->still_yuv_buffer = std::nullopt;
  return true;
}

void AutoFramingStreamManipulator::ReturnStillCaptureResultOnThread(
    Camera3CaptureDescriptor result) {
  DCHECK(thread_.IsCurrentThread());

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, result.frame_number()) << "Still capture result:";
    for (auto& b : result.GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream);
    }
  }

  CaptureContext* ctx = GetCaptureContext(result.frame_number());
  CHECK_NE(ctx, nullptr);
  ctx->cropped_still_yuv_buffer = std::nullopt;
  ctx->has_pending_blob = false;
  if (ctx->num_pending_buffers == 0 && ctx->metadata_received &&
      !ctx->has_pending_blob) {
    RemoveCaptureContext(result.frame_number());
  }

  result_callback_.Run(std::move(result));
}

bool AutoFramingStreamManipulator::SetUpPipelineOnThread(
    uint32_t target_aspect_ratio_x, uint32_t target_aspect_ratio_y) {
  DCHECK(thread_.IsCurrentThread());

  if (!auto_framing_client_.SetUp(AutoFramingClient::Options{
          .input_size = full_frame_size_,
          .frame_rate = static_cast<double>(kRequiredVideoFrameRate),
          .target_aspect_ratio_x = target_aspect_ratio_x,
          .target_aspect_ratio_y = target_aspect_ratio_y,
          .detection_rate = options_.detection_rate,
      })) {
    return false;
  }

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

void AutoFramingStreamManipulator::UpdateFaceRectangleMetadataOnThread(
    Camera3CaptureDescriptor* result) {
  DCHECK(thread_.IsCurrentThread());

  if (!result->has_metadata()) {
    return;
  }

  const Rect<float> roi_in_active_array =
      ConvertToParentSpace(region_of_interest_, full_frame_crop_);
  const Rect<float> crop_in_active_array =
      ConvertToParentSpace(active_crop_region_, full_frame_crop_);

  std::vector<Rect<float>> face_rectangles;
  if (options_.debug) {
    // Show the detected faces, aggregated region of interest and the active
    // crop region in debug mode.
    face_rectangles = faces_;
    face_rectangles.push_back(roi_in_active_array);
    face_rectangles.push_back(crop_in_active_array);
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
    if (raw_face_rectangles.size() % 4 != 0) {
      LOGF(ERROR) << "Invalid ANDROID_STATISTICS_FACE_RECTANGLES length";
      return;
    }
    for (size_t i = 0; i < raw_face_rectangles.size(); i += 4) {
      const int32_t* rect_bound = &raw_face_rectangles[i];
      const Rect<int32_t> rect(rect_bound[0], rect_bound[1],
                               rect_bound[2] - rect_bound[0] + 1,
                               rect_bound[3] - rect_bound[1] + 1);
      const Rect<int32_t> clamped_rect =
          ClampRect(rect, Rect<int32_t>(0, 0, active_array_dimension_.width,
                                        active_array_dimension_.height));
      if (!clamped_rect.is_valid()) {
        VLOGF(1) << "Invalid face rectangle: " << rect.ToString();
        continue;
      }
      face_rectangles.push_back(ConvertToCropSpace(
          NormalizeRect(clamped_rect, active_array_dimension_),
          crop_in_active_array));
    }
  }
  std::vector<int32_t> face_coordinates;
  for (const auto& f : face_rectangles) {
    face_coordinates.push_back(static_cast<int32_t>(
        f.left * static_cast<float>(active_array_dimension_.width)));
    face_coordinates.push_back(static_cast<int32_t>(
        f.top * static_cast<float>(active_array_dimension_.height)));
    face_coordinates.push_back(static_cast<int32_t>(
        f.right() * static_cast<float>(active_array_dimension_.width)));
    face_coordinates.push_back(static_cast<int32_t>(
        f.bottom() * static_cast<float>(active_array_dimension_.height)));
  }
  if (!face_coordinates.empty() &&
      !result->UpdateMetadata<int32_t>(ANDROID_STATISTICS_FACE_RECTANGLES,
                                       face_coordinates)) {
    LOGF(ERROR) << "Cannot set ANDROID_STATISTICS_FACE_RECTANGLES";
  }
}

void AutoFramingStreamManipulator::ResetOnThread() {
  DCHECK(thread_.IsCurrentThread());

  auto_framing_client_.TearDown();
  still_capture_processor_->Reset();

  state_ = State::kOff;
  client_streams_.clear();
  full_frame_stream_ = {};
  blob_stream_ = nullptr;
  still_yuv_stream_.reset();
  yuv_stream_for_blob_ = nullptr;
  capture_contexts_.clear();
  full_frame_buffer_pool_.reset();
  still_yuv_buffer_pool_.reset();
  cropped_still_yuv_buffer_pool_.reset();
  last_timestamp_ = 0;
  timestamp_offset_ = 0;

  faces_.clear();
  region_of_interest_ = Rect<float>(0.0f, 0.0f, 1.0f, 1.0f);
  active_crop_region_ = Rect<float>(0.0f, 0.0f, 1.0f, 1.0f);
}

void AutoFramingStreamManipulator::UpdateOptionsOnThread(
    const base::Value& json_values) {
  DCHECK(thread_.IsCurrentThread());

  int filter_mode, max_video_width, max_video_height;
  float detection_rate, enable_delay, disable_delay;
  if (LoadIfExist(json_values, kMaxFullWidthKey, &max_video_width)) {
    options_.max_video_width = base::checked_cast<uint32_t>(max_video_width);
  }
  if (LoadIfExist(json_values, kMaxFullHeightKey, &max_video_height)) {
    options_.max_video_height = base::checked_cast<uint32_t>(max_video_height);
  }
  if (LoadIfExist(json_values, kOutputFilterModeKey, &filter_mode)) {
    options_.output_filter_mode = static_cast<FilterMode>(filter_mode);
  }
  if (LoadIfExist(json_values, kDetectionRateKey, &detection_rate)) {
    options_.detection_rate = std::max(detection_rate, 0.0f);
  }
  if (LoadIfExist(json_values, kEnableDelayKey, &enable_delay)) {
    options_.enable_delay = base::Seconds(enable_delay);
  }
  if (LoadIfExist(json_values, kDisableDelayKey, &disable_delay)) {
    options_.disable_delay = base::Seconds(disable_delay);
  }
  options_.enable = json_values.FindBoolKey(kEnableKey);
  LoadIfExist(json_values, kDebugKey, &options_.debug);

  VLOGF(1) << "AutoFramingStreamManipulator options:"
           << " max_video_width="
           << (options_.max_video_width.has_value()
                   ? base::NumberToString(*options_.max_video_width)
                   : "(not set)")
           << " max_video_height="
           << (options_.max_video_height.has_value()
                   ? base::NumberToString(*options_.max_video_height)
                   : "(not set)")
           << " output_filter_mode="
           << static_cast<int>(options_.output_filter_mode)
           << " detection_rate=" << options_.detection_rate
           << " enable_delay=" << options_.enable_delay
           << " disable_delay=" << options_.disable_delay << " enable="
           << (options_.enable ? base::NumberToString(*options_.enable)
                               : "(not set)")
           << " debug=" << options_.debug;
}

void AutoFramingStreamManipulator::OnOptionsUpdated(
    const base::Value& json_values) {
  thread_.PostTaskAsync(
      FROM_HERE,
      base::BindOnce(&AutoFramingStreamManipulator::UpdateOptionsOnThread,
                     base::Unretained(this), json_values.Clone()));
}

std::pair<AutoFramingStreamManipulator::State,
          AutoFramingStreamManipulator::State>
AutoFramingStreamManipulator::StateTransitionOnThread() {
  DCHECK(thread_.IsCurrentThread());

  const State prev_state = state_;
  if (GetEnabled()) {
    if (state_ == State::kOff || state_ == State::kTransitionToOff) {
      state_ = State::kTransitionToOn;
      state_transition_timer_ = base::ElapsedTimer();
    } else if (state_ == State::kTransitionToOn &&
               state_transition_timer_.Elapsed() >= options_.enable_delay) {
      state_ = State::kOn;
    }
  } else {
    if (state_ == State::kOn || state_ == State::kTransitionToOn) {
      state_ = State::kTransitionToOff;
      state_transition_timer_ = base::ElapsedTimer();
    } else if (state_ == State::kTransitionToOff &&
               state_transition_timer_.Elapsed() >= options_.disable_delay) {
      state_ = State::kOff;
    }
  }
  if (prev_state != state_) {
    LOGF(INFO) << "State: " << static_cast<int>(prev_state) << " -> "
               << static_cast<int>(state_);
  }
  return std::make_pair(prev_state, state_);
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

void AutoFramingStreamManipulator::RemoveCaptureContext(uint32_t frame_number) {
  capture_contexts_.erase(frame_number);
}

base::ScopedFD AutoFramingStreamManipulator::CropBufferOnThread(
    buffer_handle_t input_yuv,
    base::ScopedFD input_release_fence,
    buffer_handle_t output_yuv,
    base::ScopedFD output_acquire_fence,
    const Rect<float>& crop_region) {
  DCHECK(thread_.IsCurrentThread());

  if (input_release_fence.is_valid() &&
      sync_wait(input_release_fence.get(), kSyncWaitTimeoutMs) != 0) {
    LOGF(ERROR) << "sync_wait() timed out on input buffer";
  }
  if (output_acquire_fence.is_valid() &&
      sync_wait(output_acquire_fence.get(), kSyncWaitTimeoutMs) != 0) {
    LOGF(ERROR) << "sync_wait() timed out on output buffer";
  }

  SharedImage input_image = SharedImage::CreateFromBuffer(
      input_yuv, Texture2D::Target::kTarget2D, true);
  SharedImage output_image = SharedImage::CreateFromBuffer(
      output_yuv, Texture2D::Target::kTarget2D, true);
  image_processor_->CropYuv(input_image.y_texture(), input_image.uv_texture(),
                            crop_region, output_image.y_texture(),
                            output_image.uv_texture(),
                            options_.output_filter_mode);

  EglFence fence;
  return fence.GetNativeFd();
}

}  // namespace cros
