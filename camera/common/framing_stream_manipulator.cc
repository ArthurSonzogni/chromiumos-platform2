/*
 * Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/framing_stream_manipulator.h"

#include <sync/sync.h>

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <optional>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/containers/contains.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/strings/string_number_conversions.h>
#include <base/system/sys_info.h>
#include <base/task/bind_post_task.h>
#include <ml_core/dlc/dlc_ids.h>
#include <sys/types.h>

#include "common/camera_hal3_helpers.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_buffer_utils.h"
#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/constants.h"
#include "cutils/native_handle.h"
#include "features/auto_framing/tracing.h"
#include "gpu/egl/egl_fence.h"
#include "gpu/shared_image.h"

namespace cros {

namespace {

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
constexpr uint32_t kStillYuvBufferUsage = GRALLOC_USAGE_HW_CAMERA_WRITE |
                                          GRALLOC_USAGE_HW_TEXTURE |
                                          kStillCaptureUsageFlag;
constexpr int kSyncWaitTimeoutMs = 300;

#if USE_CAMERA_FEATURE_SUPER_RES
inline int DivideRoundUp(int dividend, int divisor) {
  CHECK_GT(divisor, 0);
  return (dividend + divisor - 1) / divisor;
}

// Return true if single frame super resolution is enabled.
bool CanCreateUpsampler() {
  if (base::PathExists(base::FilePath(constants::kForceDisableSuperResPath))) {
    return false;
  }
  return base::PathExists(base::FilePath(constants::kForceEnableSuperResPath));
}

// Return true if we enable the evaluation mode for still capture.
bool EnableEvalStillYuv() {
  return base::PathExists(
      base::FilePath(constants::kForceEnableSuperResEvalPath));
}

// Ensure even input dimensions for GPU cropping.
std::pair<uint32_t, uint32_t> GetEvenInputDimensions(
    const Rect<float>& crop_region, const Size& dimension) {
  const uint32_t crop_width =
      DivideRoundUp(crop_region.width * dimension.width, 2) * 2;
  const uint32_t crop_height =
      DivideRoundUp(crop_region.height * dimension.height, 2) * 2;
  return std::make_pair(crop_width, crop_height);
}

// Check if the request can be applied upsampling.
bool IsUpsampleRequestValid(uint32_t target_width,
                            uint32_t target_height,
                            const Rect<float>& crop_region,
                            const Size& dimension) {
  auto [crop_width, crop_height] =
      GetEvenInputDimensions(crop_region, dimension);
  return target_width > crop_width && target_height > crop_height;
}

using ScaleMethod = FramingStreamManipulator::ScaleMethod;
std::string ScaleMethodToString(ScaleMethod method) {
  switch (method) {
    case ScaleMethod::kBicubic:
      return "bicubic";
    case ScaleMethod::kLanczos:
      return "lanczos";
    case ScaleMethod::kRaisr:
      return "raisr";
    case ScaleMethod::kLancet:
      return "lancet";
    case ScaleMethod::kLancetAlpha:
      return "lancet_alpha";
    case ScaleMethod::kInvalid:
      return "invalid";
  }
}

ResamplingMethod ScaleMethodToResamplingMethod(ScaleMethod method) {
  switch (method) {
    case ScaleMethod::kLanczos:
      return ResamplingMethod::kLanczos;
    case ScaleMethod::kRaisr:
      return ResamplingMethod::kRaisr;
    case ScaleMethod::kLancet:
    case ScaleMethod::kLancetAlpha:
      return ResamplingMethod::kLancet;
    default:
      return ResamplingMethod::kLanczos;
  }
}
#endif  // USE_CAMERA_FEATURE_SUPER_RES

std::vector<StreamFormat> GetAvailableOutputYuvFormats(
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
        format == HAL_PIXEL_FORMAT_YCBCR_420_888) {
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

// Find a full frame resolution from available formats that can generate all the
// destination streams.
std::optional<Size> GetFullFrameResolution(
    base::span<const StreamFormat> available_formats,
    base::span<const camera3_stream_t* const> dst_streams) {
  base::flat_map<const StreamFormat*, const camera3_stream_t*>
      format_to_dst_stream;
  for (auto* s : dst_streams) {
    const StreamFormat* matching_format = nullptr;
    for (auto& f : available_formats) {
      if (s->width == f.width && s->height == f.height) {
        matching_format = &f;
        break;
      }
    }
    CHECK_NE(matching_format, nullptr) << GetDebugString(s);
    format_to_dst_stream[matching_format] = s;
  }

  auto can_generate_stream = [](const StreamFormat& src_format,
                                const StreamFormat& dst_format) {
    // Strictly speaking we can't generate stream from lower fps to higher fps,
    // but in practice we only stream in video and photo speeds. Quantize the
    // frame rates to allow more formats to be generated.
    auto index_fps = [](float fps) {
      return fps + 0.1f >= kRequiredVideoFrameRate ? 1 : 0;
    };
    return src_format.fov.Covers(dst_format.fov) &&
           index_fps(src_format.max_fps) >= index_fps(dst_format.max_fps);
  };
  auto can_generate_all_streams = [&](const StreamFormat& src_format) {
    for (auto& [dst_format, s] : format_to_dst_stream) {
      if (!can_generate_stream(src_format, *dst_format)) {
        return false;
      }
    }
    return true;
  };
  auto index_format = [&](const StreamFormat& f) {
    return std::make_tuple(can_generate_all_streams(f) ? 1 : 0, f.width,
                           f.height, f.max_fps);
  };
  auto it = std::max_element(
      available_formats.begin(), available_formats.end(),
      [&](auto& f1, auto& f2) { return index_format(f1) < index_format(f2); });
  CHECK(it != available_formats.end());
  return can_generate_all_streams(*it)
             ? std::make_optional<Size>(it->width, it->height)
             : std::nullopt;
}

bool IsStreamBypassed(const camera3_stream_t* stream) {
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

#if USE_CAMERA_FEATURE_AUTO_FRAMING
int CalculateMedian(const base::flat_map<int, int>& histogram) {
  DCHECK_GT(histogram.size(), 0);
  const int total_count = std::accumulate(
      histogram.begin(), histogram.end(), 0,
      [](int count, const auto& bin) { return count + bin.second; });
  DCHECK_GT(total_count, 0);
  const int half_total_count = total_count / 2;
  size_t count = 0;
  for (const auto& [v, c] : histogram) {
    count += c;
    if (count >= half_total_count) {
      return v;
    }
  }
  NOTREACHED();
  return 0;
}
#endif  // USE_CAMERA_FEATURE_AUTO_FRAMING

bool IsFullCrop(const Rect<float>& rect) {
  constexpr float kThreshold = 1e-3f;
  return rect.width >= 1.0 - kThreshold || rect.height >= 1.0 - kThreshold;
}

// Gets the crop region in the capture request, if exists and is valid, and
// normalized with active array size.
std::optional<Rect<float>> GetManualZoomRequest(
    const base::span<const int32_t>& crop_region, const Size& active_array) {
  if (crop_region.size() != 4) {
    return std::nullopt;
  }

  uint32_t active_width = active_array.width;
  uint32_t active_height = active_array.height;
  int32_t crop_x = crop_region[0];
  int32_t crop_y = crop_region[1];
  int32_t crop_width = crop_region[2];
  int32_t crop_height = crop_region[3];

  // Validate crop region.
  if (crop_x < 0 || crop_width <= 0 || crop_y < 0 || crop_height <= 0 ||
      crop_x + crop_width > active_width ||
      crop_y + crop_height > active_height) {
    VLOG(1) << "Invalid crop window specified for manual zoom";
    return std::nullopt;
  }

  // Normalize crop region with ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE.
  auto normalized_crop_region =
      Rect<float>(static_cast<float>(crop_region[0]) / active_array.width,
                  static_cast<float>(crop_region[1]) / active_array.height,
                  static_cast<float>(crop_region[2]) / active_array.width,
                  static_cast<float>(crop_region[3]) / active_array.height);

  // There is no need to do manual zoom if the crop region is the full frame.
  if (IsFullCrop(normalized_crop_region)) {
    return std::nullopt;
  }

  return normalized_crop_region;
}

uint32_t GetFullFrameBufferUsage(uint32_t yuv_stream_usage) {
#if USE_QUALCOMM_CAMX
  // Some HALs expect all YUV streams to set the usage of
  // GRALLOC_USAGE_HW_VIDEO_ENCODER consistently.
  if ((yuv_stream_usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) ==
      GRALLOC_USAGE_HW_VIDEO_ENCODER) {
    return kFullFrameBufferUsage | GRALLOC_USAGE_HW_VIDEO_ENCODER;
  }
#endif
  return kFullFrameBufferUsage;
}

}  // namespace

//
// FramingStreamManipulator implementations.
//

struct FramingStreamManipulator::CaptureContext {
  std::pair<State, State> state_transition;
  bool bypassed = false;
  uint32_t num_pending_buffers = 0;
  bool metadata_received = false;
  bool has_pending_blob = false;
  std::vector<camera3_stream_buffer_t> client_video_yuv_buffers;
  std::optional<camera3_stream_buffer_t> client_still_yuv_buffer;
  std::optional<CameraBufferPool::Buffer> full_frame_buffer;
  std::optional<CameraBufferPool::Buffer> still_yuv_buffer;
  std::optional<CameraBufferPool::Buffer> cropped_still_yuv_buffer;
  std::optional<int64_t> timestamp;
  std::optional<Rect<float>> crop_region;
  // Crop region from capture request. This value is needed to fill back in the
  // capture result after we temporarily delete it in the capture request.
  std::optional<Rect<uint32_t>> requested_crop_region;
};

FramingStreamManipulator::FramingStreamManipulator(
    RuntimeOptions* runtime_options,
    GpuResources* gpu_resources,
    base::FilePath config_file_path,
    std::unique_ptr<StillCaptureProcessor> still_capture_processor,
    std::optional<Options> options_override_for_testing,
    bool auto_framing_supported)
    : config_(ReloadableConfigFile::Options{
          .default_config_file_path = std::move(config_file_path),
          .override_config_file_path =
              base::FilePath(kOverrideAutoFramingConfigFile)}),
      runtime_options_(runtime_options),
      gpu_resources_(gpu_resources),
      still_capture_processor_(std::move(still_capture_processor)),
      camera_metrics_(CameraMetrics::New()),
      auto_framing_supported_(auto_framing_supported) {
  DCHECK_NE(runtime_options_, nullptr);
  DCHECK(gpu_resources_);
  TRACE_AUTO_FRAMING();

  if (options_override_for_testing) {
    options_ = *options_override_for_testing;
  } else {
    if (!config_.IsValid()) {
      LOGF(ERROR) << "Cannot load valid config; turn off feature by default";
      options_.enable = false;
    }
    config_.SetCallback(base::BindRepeating(
        &FramingStreamManipulator::OnOptionsUpdated, base::Unretained(this)));
  }

#if USE_CAMERA_FEATURE_SUPER_RES
  const base::FilePath dlc_root_path =
      runtime_options_->GetDlcRootPath(dlc_client::kSuperResDlcId);
  if (CanCreateUpsampler() && !dlc_root_path.empty()) {
    CreateUpsampler(dlc_root_path);
  }
#endif  // USE_CAMERA_FEATURE_SUPER_RES
}

FramingStreamManipulator::~FramingStreamManipulator() {
  TRACE_AUTO_FRAMING();

  gpu_resources_->PostGpuTaskSync(
      FROM_HERE, base::BindOnce(&FramingStreamManipulator::ResetOnThread,
                                base::Unretained(this)));
}

bool FramingStreamManipulator::UpdateVendorTags(
    VendorTagManager& vendor_tag_manager) {
  if (!vendor_tag_manager.Add(kCrosDigitalZoomVendorKey,
                              kCrosDigitalZoomVendorTagSectionName,
                              kCrosDigitalZoomVendorTagName, TYPE_BYTE) ||
      !vendor_tag_manager.Add(kCrosDigitalZoomRequestedVendorKey,
                              kCrosDigitalZoomVendorTagSectionName,
                              kCrosDigitalZoomRequestedVendorTagName,
                              TYPE_BYTE)) {
    LOGF(ERROR) << "Failed to add digital zoom vendor tags";
    return false;
  }
  return true;
}

bool FramingStreamManipulator::UpdateStaticMetadata(
    android::CameraMetadata* static_info) {
  if (!FramingStreamManipulator::IsManualZoomSupported()) {
    VLOGF(1) << "Manual zoom is not supported on this device.";
    return true;
  }

  camera_metadata_entry_t facing_entry = static_info->find(ANDROID_LENS_FACING);
  bool is_external = facing_entry.count > 0 &&
                     facing_entry.data.u8[0] == ANDROID_LENS_FACING_EXTERNAL;
  if (is_external) {
    VLOGF(1) << "Manual zoom is not supported on external cameras";
    return true;
  }

  uint8_t can_attempt_digital_zoom = 1;
  if (static_info->update(kCrosDigitalZoomVendorKey, &can_attempt_digital_zoom,
                          1) != 0) {
    LOGF(ERROR) << "Failed to update kCrosDigitalZoomVendorKey";
    return false;
  }

  return true;
}

bool FramingStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  bool ret;
  gpu_resources_->PostGpuTaskSync(
      FROM_HERE,
      base::BindOnce(&FramingStreamManipulator::InitializeOnThread,
                     base::Unretained(this), static_info, std::move(callbacks)),
      &ret);
  return ret;
}

bool FramingStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  bool ret;
  gpu_resources_->PostGpuTaskSync(
      FROM_HERE,
      base::BindOnce(&FramingStreamManipulator::ConfigureStreamsOnThread,
                     base::Unretained(this), stream_config),
      &ret);
  return ret;
}

bool FramingStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  bool ret;
  gpu_resources_->PostGpuTaskSync(
      FROM_HERE,
      base::BindOnce(&FramingStreamManipulator::OnConfiguredStreamsOnThread,
                     base::Unretained(this), stream_config),
      &ret);
  return ret;
}

bool FramingStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  // TODO(jcliang): Fill in the PTZ vendor tags.
  return true;
}

bool FramingStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  bool ret;
  gpu_resources_->PostGpuTaskSync(
      FROM_HERE,
      base::BindOnce(&FramingStreamManipulator::ProcessCaptureRequestOnThread,
                     base::Unretained(this), request),
      &ret);
  return ret;
}

bool FramingStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor result) {
  bool ret;
  gpu_resources_->PostGpuTaskSync(
      FROM_HERE,
      base::BindOnce(&FramingStreamManipulator::ProcessCaptureResultOnThread,
                     base::Unretained(this), &result),
      &ret);
  callbacks_.result_callback.Run(std::move(result));
  return ret;
}

void FramingStreamManipulator::Notify(camera3_notify_msg_t msg) {
  TRACE_AUTO_FRAMING();

  callbacks_.notify_callback.Run(std::move(msg));
}

bool FramingStreamManipulator::Flush() {
  TRACE_AUTO_FRAMING();

  return true;
}

bool FramingStreamManipulator::IsManualZoomSupported() {
  if (base::PathExists(base::FilePath(kForceDisableManualZoomPath))) {
    return false;
  }
  if (base::PathExists(base::FilePath(kForceEnableManualZoomPath))) {
    return true;
  }
  return true;
}

#if USE_CAMERA_FEATURE_SUPER_RES
void FramingStreamManipulator::CreateUpsampler(
    const base::FilePath& dlc_root_path) {
  DCHECK(!single_frame_upsampler_);
  single_frame_upsampler_ = std::make_unique<SingleFrameUpsampler>();

  LOGF(INFO) << "Single frame super resolution is enabled.";
  if (!single_frame_upsampler_->Initialize(dlc_root_path)) {
    LOGF(ERROR) << "Failed to initialize SingleFrameUpsampler";
    single_frame_upsampler_ = nullptr;
  }
}
#endif  // USE_CAMERA_FEATURE_SUPER_RES

bool FramingStreamManipulator::InitializeOnThread(
    const camera_metadata_t* static_info,
    StreamManipulator::Callbacks callbacks) {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  DCHECK(!callbacks.result_callback.is_null());
  TRACE_AUTO_FRAMING();

  callbacks_ = std::move(callbacks);
  setup_failed_ = false;

  partial_result_count_ = GetPartialResultCount(static_info);

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
    setup_failed_ = true;
    ++metrics_.errors[AutoFramingError::kInitializationError];
    return false;
  }

  available_formats_ =
      GetAvailableOutputYuvFormats(static_info, active_array_dimension_);

  std::optional<uint8_t> vendor_tag =
      GetRoMetadata<uint8_t>(static_info, kCrosDigitalZoomVendorKey);
  manual_zoom_supported_ = vendor_tag.has_value() && *vendor_tag == 1;

  return true;
}

bool FramingStreamManipulator::ConfigureStreamsOnThread(
    Camera3StreamConfiguration* stream_config) {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_AUTO_FRAMING([&](perfetto::EventContext ctx) {
    stream_config->PopulateEventAnnotation(ctx);
  });

  // Check from session parameters in the stream config if manual zoom is
  // requested from the camera client.
  session_parameters_ =
      clone_camera_metadata(stream_config->session_parameters());
  auto request_entry =
      session_parameters_.find(kCrosDigitalZoomRequestedVendorKey);
  manual_zoom_requested_ = manual_zoom_supported_ && request_entry.count > 0 &&
                           request_entry.data.i32[0] == 1;
  // Clean up the custom vendor tag in the stream config before sending to the
  // HAL to prevent "invalid vendor tag" crash. See b/330110878 for details.
  if (request_entry.count > 0) {
    session_parameters_.erase(kCrosDigitalZoomRequestedVendorKey);
    stream_config->SetSessionParameters(session_parameters_.getAndLock());
  }

  if (setup_failed_) {
    return false;
  }
  ResetOnThread();

  // Skip stream configuration if neither auto framing nor manual zoom will be
  // performed throughout the session.
  stream_config_skipped_ = !auto_framing_supported_ && !manual_zoom_requested_;
  if (stream_config_skipped_) {
    VLOGF(1) << "Skip configuring streams as there is no usage";
    return true;
  }

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "Config streams from client:";
    for (auto* s : stream_config->GetStreams()) {
      VLOGF(1) << "  " << GetDebugString(s);
    }
  }

  // Filter client streams into |hal_streams| that will be requested to the HAL.
  client_streams_ = CopyToVector(stream_config->GetStreams());
  camera3_stream_t* client_still_yuv_stream = nullptr;
  std::vector<const camera3_stream_t*> client_video_yuv_streams;
  std::vector<camera3_stream_t*> hal_streams;
  Size target_size;
  char* physical_camera_id;
  uint32_t full_frame_buffer_usage = kFullFrameBufferUsage;
  for (auto* s : client_streams_) {
    if (IsStreamBypassed(s)) {
      hal_streams.push_back(s);
      continue;
    }
    if (s->format == HAL_PIXEL_FORMAT_BLOB) {
      // Process the BLOB stream inplace.
      still_capture_processor_->Initialize(
          s,
          base::BindPostTask(
              gpu_resources_->gpu_task_runner(),
              base::BindRepeating(
                  &FramingStreamManipulator::ReturnStillCaptureResultOnThread,
                  base::Unretained(this))));
      blob_stream_ = s;
      hal_streams.push_back(s);
    } else {
      CHECK(s->format == HAL_PIXEL_FORMAT_YCBCR_420_888 ||
            s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED);
      if (s->usage & kStillCaptureUsageFlag) {
        CHECK_EQ(client_still_yuv_stream, nullptr);
        client_still_yuv_stream = s;
        hal_streams.push_back(s);
      } else {
        client_video_yuv_streams.push_back(s);
      }
    }
    if (s->format == HAL_PIXEL_FORMAT_YCBCR_420_888) {
      full_frame_buffer_usage = GetFullFrameBufferUsage(s->usage);
    }
    // Choose the output stream of the largest resolution for matching the crop
    // window aspect ratio. Prefer taller size since extending crop windows
    // horizontally (for other outputs) looks better.
    if (!target_size.is_valid() || s->height > target_size.height ||
        (s->height == target_size.height && s->width > target_size.width)) {
      target_size = Size(s->width, s->height);
      // Assign physical camera id to use in |full_frame_stream_|.
      physical_camera_id = const_cast<char*>(s->physical_camera_id);
    }
  }
  if (!target_size.is_valid()) {
    LOGF(ERROR) << "No valid output stream found in stream config";
    setup_failed_ = true;
    ++metrics_.errors[AutoFramingError::kConfigurationError];
    return false;
  }
  auto [target_aspect_ratio_x, target_aspect_ratio_y] =
      GetAspectRatio(target_size);
  VLOGF(1) << "Target output aspect ratio: " << target_aspect_ratio_x << ":"
           << target_aspect_ratio_y;
  active_crop_region_ = NormalizeRect(
      GetCenteringFullCrop(active_array_dimension_, target_aspect_ratio_x,
                           target_aspect_ratio_y),
      active_array_dimension_);

  const std::optional<Size> size =
      GetFullFrameResolution(available_formats_, client_video_yuv_streams);
  if (!size.has_value()) {
    LOGF(ERROR) << "Can't find suitable resolution for full frame stream";
    setup_failed_ = true;
    ++metrics_.errors[AutoFramingError::kConfigurationError];
    return false;
  }
  full_frame_size_ = size.value();
  VLOGF(1) << "Full frame size: " << full_frame_size_.ToString();

  full_frame_crop_ = NormalizeRect(
      GetCenteringFullCrop(active_array_dimension_, full_frame_size_.width,
                           full_frame_size_.height),
      active_array_dimension_);

  // Reuse or create still YUV stream for processing still capture.
  if (blob_stream_ != nullptr) {
    if (client_still_yuv_stream != nullptr) {
      yuv_stream_for_blob_ = client_still_yuv_stream;
      yuv_stream_for_blob_->usage |= kStillYuvBufferUsage;
    } else {
      still_yuv_stream_ = std::make_unique<camera3_stream_t>(camera3_stream_t{
          .stream_type = CAMERA3_STREAM_OUTPUT,
          .width = blob_stream_->width,
          .height = blob_stream_->height,
          .format = HAL_PIXEL_FORMAT_YCbCr_420_888,
          .usage = kStillYuvBufferUsage,
          .physical_camera_id = blob_stream_->physical_camera_id,
      });
      hal_streams.push_back(still_yuv_stream_.get());
      yuv_stream_for_blob_ = still_yuv_stream_.get();
    }
  }

  // Create a stream to run auto-framing on.
  full_frame_stream_ = camera3_stream_t{
      .stream_type = CAMERA3_STREAM_OUTPUT,
      .width = full_frame_size_.width,
      .height = full_frame_size_.height,
      .format = HAL_PIXEL_FORMAT_YCbCr_420_888,
      .usage = full_frame_buffer_usage,
      .physical_camera_id = physical_camera_id,
  };
  hal_streams.push_back(&full_frame_stream_);

  if (!stream_config->SetStreams(hal_streams)) {
    LOGF(ERROR) << "Failed to manipulate stream config";
    setup_failed_ = true;
    ++metrics_.errors[AutoFramingError::kConfigurationError];
    return false;
  }

  if (auto_framing_supported_ &&
      !SetUpPipelineOnThread(target_aspect_ratio_x, target_aspect_ratio_y)) {
    setup_failed_ = true;
    return false;
  }

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "Config streams to HAL:";
    for (auto* s : stream_config->GetStreams()) {
      VLOGF(1) << "  " << GetDebugString(s);
    }
    if (yuv_stream_for_blob_) {
      VLOGF(1) << "YUV for BLOB: " << GetDebugString(yuv_stream_for_blob_)
               << ", owned: " << (still_yuv_stream_ ? "YES" : "NO");
    }
  }

  return true;
}

bool FramingStreamManipulator::OnConfiguredStreamsOnThread(
    Camera3StreamConfiguration* stream_config) {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_AUTO_FRAMING([&](perfetto::EventContext ctx) {
    stream_config->PopulateEventAnnotation(ctx);
  });

  if (setup_failed_ || stream_config_skipped_) {
    return !setup_failed_;
  }

  if (VLOG_IS_ON(1)) {
    VLOGF(1) << "Configured streams from HAL:";
    for (auto* s : stream_config->GetStreams()) {
      VLOGF(1) << "  " << GetDebugString(s);
    }
  }

  if ((full_frame_stream_.usage & kFullFrameBufferUsage) !=
      kFullFrameBufferUsage) {
    LOGF(ERROR) << "Failed to negotiate buffer usage on full frame stream";
    setup_failed_ = true;
    ++metrics_.errors[AutoFramingError::kConfigurationError];
    return false;
  }
  full_frame_buffer_pool_ =
      std::make_unique<CameraBufferPool>(CameraBufferPool::Options{
          .width = full_frame_stream_.width,
          .height = full_frame_stream_.height,
          .format = base::checked_cast<uint32_t>(full_frame_stream_.format),
          .usage = full_frame_stream_.usage,
          // TODO(b/328541174): We will get full frame buffer allocation errors
          // during still capture request. Temporarily increase the buffer count
          // as a workaround.
          .max_num_buffers =
              base::strict_cast<size_t>(full_frame_stream_.max_buffers) + 2,
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

  if (yuv_stream_for_blob_) {
    if ((yuv_stream_for_blob_->usage & kStillYuvBufferUsage) !=
        kStillYuvBufferUsage) {
      LOGF(ERROR) << "Failed to negotiate buffer usage on still YUV stream";
      setup_failed_ = true;
      ++metrics_.errors[AutoFramingError::kConfigurationError];
      return false;
    }
    still_yuv_buffer_pool_ =
        std::make_unique<CameraBufferPool>(CameraBufferPool::Options{
            .width = yuv_stream_for_blob_->width,
            .height = yuv_stream_for_blob_->height,
            .format =
                base::checked_cast<uint32_t>(yuv_stream_for_blob_->format),
            .usage = yuv_stream_for_blob_->usage,
            .max_num_buffers =
                base::strict_cast<size_t>(yuv_stream_for_blob_->max_buffers) +
                1,
        });
  }

  // Configure the video YUV streams that are not passed to the HAL.
  for (auto* s : client_streams_) {
    if (!IsStreamBypassed(s) &&
        (s->format == HAL_PIXEL_FORMAT_YCBCR_420_888 ||
         s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) &&
        !(s->usage & kStillCaptureUsageFlag)) {
      s->usage |= GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_CAMERA_WRITE;
      s->max_buffers = full_frame_stream_.max_buffers;
    }
  }

  if (!stream_config->SetStreams(client_streams_)) {
    LOGF(ERROR) << "Failed to recover stream config";
    setup_failed_ = true;
    ++metrics_.errors[AutoFramingError::kConfigurationError];
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

bool FramingStreamManipulator::GetAutoFramingEnabled() {
  // Use option in config file first.
  // TODO(pihsun): Handle multi people mode.
  // TODO(pihsun): ReloadableConfigFile merges new config to old config, so
  // this won't be "unset" after set, which will be confusing for developers.
  return auto_framing_supported_ &&
         options_.enable.value_or(runtime_options_->sw_privacy_switch_state() !=
                                      mojom::CameraPrivacySwitchState::ON &&
                                  runtime_options_->auto_framing_state() !=
                                      mojom::CameraAutoFramingState::OFF);
}

bool FramingStreamManipulator::ProcessCaptureRequestOnThread(
    Camera3CaptureDescriptor* request) {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_AUTO_FRAMING("frame_number", request->frame_number());

  if (setup_failed_ || stream_config_skipped_) {
    return !setup_failed_;
  }

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, request->frame_number())
        << "Request stream buffers from client:";
    for (auto& b : request->GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream());
    }
  }

  ++metrics_.num_captures;

  const base::span<const int32_t>& requested_crop_region =
      request->GetMetadata<int32_t>(ANDROID_SCALER_CROP_REGION);
  auto normalized_crop_region =
      GetManualZoomRequest(requested_crop_region, active_array_dimension_);
  // Manual zoom is considered enabled when:
  // 1. The device and camera support.
  // 2. Manual zoom is requested when configuring streams.
  // 3. Valid crop region is sent with the capture request.
  bool manual_zoom_enabled =
      manual_zoom_requested_ && normalized_crop_region.has_value();

  const std::pair<State, State> state_transition =
      StateTransitionOnThread(manual_zoom_enabled);

  CaptureContext* ctx = CreateCaptureContext(request->frame_number());
  if (!ctx) {
    ++metrics_.errors[AutoFramingError::kProcessRequestError];
    return false;
  }
  ctx->state_transition = state_transition;

  // Bypass reprocessing requests and all requests when in |kDisabled| state.
  if (request->has_input_buffer() ||
      state_transition.second == State::kDisabled) {
    ctx->bypassed = true;
    // Replace video streams by full frame stream.
    for (auto& b : request->AcquireOutputBuffers()) {
      if (IsStreamBypassed(b.stream()) || b.stream() == blob_stream_ ||
          b.stream() == yuv_stream_for_blob_) {
        request->AppendOutputBuffer(std::move(b));
      } else {
        ctx->client_video_yuv_buffers.push_back(b.raw_buffer());
      }
    }
    if (!ctx->client_video_yuv_buffers.empty()) {
      ctx->full_frame_buffer = full_frame_buffer_pool_->RequestBuffer();
      CHECK(ctx->full_frame_buffer.has_value());
      request->AppendOutputBuffer(Camera3StreamBuffer::MakeRequestOutput({
          .stream = &full_frame_stream_,
          .buffer = ctx->full_frame_buffer->handle(),
          .status = CAMERA3_BUFFER_STATUS_OK,
          .acquire_fence = -1,
          .release_fence = -1,
      }));
    }
    return true;
  }

  if (state_transition.second == State::kManualZoom) {
    ctx->crop_region =
        ConvertToCropSpace(normalized_crop_region.value(), full_frame_crop_);
    ctx->requested_crop_region =
        Rect<uint32_t>(requested_crop_region[0], requested_crop_region[1],
                       requested_crop_region[2], requested_crop_region[3]);
    // Remove crop region from the capture request to avoid cropping twice in
    // the devices with HAL-level zoom support.
    request->DeleteMetadata(ANDROID_SCALER_CROP_REGION);
  }

  // Separate the buffers that will be done by us from the ones that will be
  // sent to the HAL.
  for (auto& b : request->AcquireOutputBuffers()) {
    if (IsStreamBypassed(b.stream())) {
      request->AppendOutputBuffer(std::move(b));
    } else if (b.stream() == blob_stream_) {
      ctx->has_pending_blob = true;
      still_capture_processor_->QueuePendingRequest(request->frame_number(),
                                                    *request);
      if (b.raw_buffer().buffer != nullptr) {
        still_capture_processor_->QueuePendingOutputBuffer(
            request->frame_number(), b.mutable_raw_buffer());
      }
      request->AppendOutputBuffer(std::move(b));
    } else if (b.stream()->usage & kStillCaptureUsageFlag) {
      CHECK(!ctx->client_still_yuv_buffer.has_value());
      ctx->client_still_yuv_buffer.emplace(b.raw_buffer());
    } else {
      ctx->client_video_yuv_buffers.push_back(b.raw_buffer());
    }
  }
  CHECK(ctx->has_pending_blob || !ctx->client_still_yuv_buffer.has_value());

  // Add full frame output.
  if (!ctx->client_video_yuv_buffers.empty()) {
    DCHECK_NE(full_frame_buffer_pool_, nullptr);
    ctx->full_frame_buffer = full_frame_buffer_pool_->RequestBuffer();
    if (!ctx->full_frame_buffer) {
      LOGF(ERROR) << "Failed to allocate full frame buffer for request "
                  << request->frame_number();
      ++metrics_.errors[AutoFramingError::kProcessRequestError];
      return false;
    }
    request->AppendOutputBuffer(Camera3StreamBuffer::MakeRequestOutput({
        .stream = &full_frame_stream_,
        .buffer = ctx->full_frame_buffer->handle(),
        .status = CAMERA3_BUFFER_STATUS_OK,
        .acquire_fence = -1,
        .release_fence = -1,
    }));
  }

  // Add still YUV output.
  if (ctx->has_pending_blob) {
    DCHECK_NE(still_yuv_buffer_pool_, nullptr);
    ctx->still_yuv_buffer = still_yuv_buffer_pool_->RequestBuffer();
    if (!ctx->still_yuv_buffer) {
      LOGF(ERROR) << "Failed to allocate still YUV buffer for request "
                  << request->frame_number();
      ++metrics_.errors[AutoFramingError::kProcessRequestError];
      return false;
    }
    request->AppendOutputBuffer(Camera3StreamBuffer::MakeRequestOutput({
        .stream = yuv_stream_for_blob_,
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

bool FramingStreamManipulator::ProcessCaptureResultOnThread(
    Camera3CaptureDescriptor* result) {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_AUTO_FRAMING("frame_number", result->frame_number());

  if (setup_failed_ || stream_config_skipped_) {
    return !setup_failed_;
  }

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, result->frame_number()) << "Result stream buffers from HAL:";
    for (auto& b : result->GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream());
    }
  }

  CaptureContext* ctx = GetCaptureContext(result->frame_number());
  if (!ctx) {
    // This capture is bypassed.
    return true;
  }

  if (ctx->bypassed) {
    // Scale the full frame into client video yuv buffers.
    std::optional<Camera3StreamBuffer> full_frame_buffer;
    for (auto& b : result->AcquireOutputBuffers()) {
      if (b.stream() == &full_frame_stream_) {
        full_frame_buffer.emplace(std::move(b));
      } else {
        result->AppendOutputBuffer(std::move(b));
      }
    }
    if (full_frame_buffer.has_value()) {
      if (full_frame_buffer->status() == CAMERA3_BUFFER_STATUS_OK) {
        for (auto& b : ctx->client_video_yuv_buffers) {
          std::optional<base::ScopedFD> fence = CropAndScaleOnThread(
              *full_frame_buffer->buffer(),
              base::ScopedFD(full_frame_buffer->take_release_fence()),
              *b.buffer, base::ScopedFD(b.acquire_fence),
              RelativeFov(full_frame_size_, active_array_dimension_)
                  .GetCropWindowInto(
                      RelativeFov(Size(b.stream->width, b.stream->height),
                                  active_array_dimension_)),
              ScaleMethod::kBicubic);
          b.acquire_fence = -1;
          if (fence.has_value()) {
            b.release_fence = fence->release();
          } else {
            b.status = CAMERA3_BUFFER_STATUS_ERROR;
          }
          result->AppendOutputBuffer(Camera3StreamBuffer::MakeResultOutput(b));
        }
      } else {
        for (auto& b : ctx->client_video_yuv_buffers) {
          b.status = CAMERA3_BUFFER_STATUS_ERROR;
          result->AppendOutputBuffer(Camera3StreamBuffer::MakeResultOutput(b));
        }
      }
      ctx->client_video_yuv_buffers.clear();
      ctx->full_frame_buffer.reset();
    }
    if (ctx->client_video_yuv_buffers.empty()) {
      RemoveCaptureContext(result->frame_number());
    }
    return true;
  }

  if (ctx->state_transition.second == State::kManualZoom &&
      result->partial_result() != 0) {
    if (ctx->requested_crop_region.has_value()) {
      // Fill the crop region, which is temporarily deleted, back to the capture
      // result. This is done to conform the API as it expects the capture
      // result to contain the crop region that aligns with the request.
      result->UpdateMetadata(
          ANDROID_SCALER_CROP_REGION,
          base::span<const int32_t>(
              {static_cast<const int32_t>(ctx->requested_crop_region->left),
               static_cast<const int32_t>(ctx->requested_crop_region->top),
               static_cast<const int32_t>(ctx->requested_crop_region->width),
               static_cast<const int32_t>(
                   ctx->requested_crop_region->height)}));
      ctx->requested_crop_region.reset();
    } else {
      result->DeleteMetadata(ANDROID_SCALER_CROP_REGION);
    }
  }

  CHECK_GE(ctx->num_pending_buffers, result->num_output_buffers());
  ctx->num_pending_buffers -= result->num_output_buffers();
  ctx->metadata_received |= result->partial_result() == partial_result_count_;

  base::ScopedClosureRunner ctx_deleter;
  if (ctx->num_pending_buffers == 0 && ctx->metadata_received &&
      !ctx->has_pending_blob) {
    ctx_deleter.ReplaceClosure(
        base::BindOnce(&FramingStreamManipulator::RemoveCaptureContext,
                       base::Unretained(this), result->frame_number()));
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

  std::optional<Camera3StreamBuffer> full_frame_buffer;
  std::optional<Camera3StreamBuffer> still_yuv_buffer;
  std::optional<Camera3StreamBuffer> blob_buffer;
  for (auto& b : result->AcquireOutputBuffers()) {
    if (IsStreamBypassed(b.stream())) {
      result->AppendOutputBuffer(std::move(b));
      continue;
    }

    if (b.stream() == &full_frame_stream_) {
      // Take the full frame buffer we inserted for processing.
      full_frame_buffer = std::move(b);
    } else if (yuv_stream_for_blob_ && b.stream() == yuv_stream_for_blob_) {
      // Take the still YUV buffer we inserted for processing.
      still_yuv_buffer = std::move(b);
    } else if (blob_stream_ && b.stream() == blob_stream_) {
      // Intercept the output BLOB for still capture processing.
      blob_buffer = std::move(b);
    }
  }

  if (full_frame_buffer) {
    const bool ok = ProcessFullFrameOnThread(ctx, std::move(*full_frame_buffer),
                                             result->frame_number());
    for (auto& b : ctx->client_video_yuv_buffers) {
      b.status = ok ? CAMERA3_BUFFER_STATUS_OK : CAMERA3_BUFFER_STATUS_ERROR;
      result->AppendOutputBuffer(Camera3StreamBuffer::MakeResultOutput(b));
    }
  }
  if (still_yuv_buffer) {
    const bool ok = ProcessStillYuvOnThread(ctx, std::move(*still_yuv_buffer),
                                            result->frame_number());
    if (!ok) {
      LOGF(ERROR) << "Failed to produce YUV image for still capture "
                  << result->frame_number();
      still_capture_processor_->CancelPendingRequest(result->frame_number());
    }
    if (ctx->client_still_yuv_buffer.has_value()) {
      ctx->client_still_yuv_buffer->status =
          ok ? CAMERA3_BUFFER_STATUS_OK : CAMERA3_BUFFER_STATUS_ERROR;
      result->AppendOutputBuffer(Camera3StreamBuffer::MakeResultOutput(
          ctx->client_still_yuv_buffer.value()));
    }
  }
  if (blob_buffer) {
    if (!still_capture_processor_->IsPendingOutputBufferQueued(
            result->frame_number())) {
      still_capture_processor_->QueuePendingOutputBuffer(
          result->frame_number(), blob_buffer->mutable_raw_buffer());
    }
    still_capture_processor_->QueuePendingAppsSegments(
        result->frame_number(), *blob_buffer->buffer(),
        base::ScopedFD(blob_buffer->take_release_fence()));
  }

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, result->frame_number()) << "Result stream buffers to client:";
    for (auto& b : result->GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream());
    }
  }

  return true;
}

bool FramingStreamManipulator::ProcessFullFrameOnThread(
    CaptureContext* ctx,
    Camera3StreamBuffer full_frame_buffer,
    uint32_t frame_number) {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  DCHECK_NE(ctx, nullptr);
  TRACE_AUTO_FRAMING(
      "frame_number", frame_number,
      perfetto::Flow::ProcessScoped(full_frame_buffer.flow_id()));

#if USE_CAMERA_FEATURE_SUPER_RES
  const base::FilePath dlc_root_path =
      runtime_options_->GetDlcRootPath(dlc_client::kSuperResDlcId);
  if (CanCreateUpsampler() && !single_frame_upsampler_ &&
      !dlc_root_path.empty()) {
    CreateUpsampler(dlc_root_path);
  }
#endif  // USE_CAMERA_FEATURE_SUPER_RES

  if (full_frame_buffer.status() != CAMERA3_BUFFER_STATUS_OK) {
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

  if (!full_frame_buffer.WaitOnAndClearReleaseFence(kSyncWaitTimeoutMs)) {
    LOGF(ERROR) << "sync_wait() HAL buffer timed out on capture result "
                << frame_number;
    ++metrics_.errors[AutoFramingError::kProcessResultError];
    return false;
  }

  if (ctx->state_transition.second != State::kManualZoom &&
      auto_framing_supported_ &&
      !GetAutoFramingCropWindowOnThread(ctx, *full_frame_buffer.buffer(),
                                        frame_number)) {
    return false;
  }

  active_crop_region_ = *ctx->crop_region;
  for (auto& b : ctx->client_video_yuv_buffers) {
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
    std::optional<base::ScopedFD> release_fence =
        CropAndScaleOnThread(*full_frame_buffer.buffer(), base::ScopedFD(),
                             *b.buffer, base::ScopedFD(b.acquire_fence),
                             adjusted_crop_region, ScaleMethod::kBicubic);
    if (!release_fence.has_value()) {
      LOGF(ERROR) << "Failed to crop buffer on result " << frame_number;
      ++metrics_.errors[AutoFramingError::kProcessResultError];
      return false;
    }
    b.acquire_fence = -1;
    b.release_fence = release_fence->release();
  }

  ctx->full_frame_buffer = std::nullopt;
  return true;
}

bool FramingStreamManipulator::ProcessStillYuvOnThread(
    CaptureContext* ctx,
    Camera3StreamBuffer still_yuv_buffer,
    uint32_t frame_number) {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  DCHECK_NE(ctx, nullptr);
  TRACE_AUTO_FRAMING("frame_number", frame_number,
                     perfetto::Flow::ProcessScoped(still_yuv_buffer.flow_id()));

#if USE_CAMERA_FEATURE_SUPER_RES
  const base::FilePath dlc_root_path =
      runtime_options_->GetDlcRootPath(dlc_client::kSuperResDlcId);
  if (CanCreateUpsampler() && !single_frame_upsampler_ &&
      !dlc_root_path.empty()) {
    CreateUpsampler(dlc_root_path);
  }
#endif  // USE_CAMERA_FEATURE_SUPER_RES

  if (still_yuv_buffer.status() != CAMERA3_BUFFER_STATUS_OK) {
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
    ++metrics_.errors[AutoFramingError::kProcessResultError];
    return false;
  }
  const Rect<float> adjusted_crop_region = AdjustCropRectToTargetAspectRatio(
      *ctx->crop_region,
      static_cast<float>(yuv_stream_for_blob_->height * blob_stream_->width) /
          static_cast<float>(yuv_stream_for_blob_->width *
                             blob_stream_->height));
  std::optional<base::ScopedFD> release_fence = CropAndScaleOnThread(
      *still_yuv_buffer.buffer(),
      base::ScopedFD(still_yuv_buffer.take_release_fence()),
      *ctx->cropped_still_yuv_buffer->handle(), base::ScopedFD(),
      adjusted_crop_region, ScaleMethod::kLancet);
  if (!release_fence.has_value()) {
    LOGF(ERROR) << "Failed to crop buffer on result " << frame_number;
    ++metrics_.errors[AutoFramingError::kProcessResultError];
    return false;
  }
  still_capture_processor_->QueuePendingYuvImage(
      frame_number, *ctx->cropped_still_yuv_buffer->handle(),
      *std::move(release_fence));

  if (ctx->client_still_yuv_buffer.has_value()) {
    std::optional<base::ScopedFD> fence = CropAndScaleOnThread(
        *still_yuv_buffer.buffer(), base::ScopedFD(),
        *ctx->client_still_yuv_buffer->buffer,
        base::ScopedFD(ctx->client_still_yuv_buffer->acquire_fence),
        AdjustCropRectToTargetAspectRatio(
            *ctx->crop_region,
            static_cast<float>(full_frame_size_.height *
                               yuv_stream_for_blob_->width) /
                static_cast<float>(full_frame_size_.width *
                                   yuv_stream_for_blob_->height)),
        ScaleMethod::kLancet);
    if (!fence.has_value()) {
      LOGF(ERROR) << "Failed to crop buffer on result " << frame_number;
      ++metrics_.errors[AutoFramingError::kProcessResultError];
      return false;
    }
    ctx->client_still_yuv_buffer->acquire_fence = -1;
    ctx->client_still_yuv_buffer->release_fence = fence->release();
  }
#if USE_CAMERA_FEATURE_SUPER_RES
  if (EnableEvalStillYuv()) {
    CHECK(ctx->timestamp.has_value());
    ProduceMultiUpsamplingResults(*ctx->timestamp, *still_yuv_buffer.buffer(),
                                  adjusted_crop_region);
  }
#endif  // USE_CAMERA_FEATURE_SUPER_RES

  ctx->still_yuv_buffer = std::nullopt;
  return true;
}

void FramingStreamManipulator::ReturnStillCaptureResultOnThread(
    Camera3CaptureDescriptor result) {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());

  if (VLOG_IS_ON(2)) {
    VLOGFID(2, result.frame_number()) << "Still capture result:";
    for (auto& b : result.GetOutputBuffers()) {
      VLOGF(2) << "  " << GetDebugString(b.stream());
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

  callbacks_.result_callback.Run(std::move(result));
}

bool FramingStreamManipulator::GetAutoFramingCropWindowOnThread(
    CaptureContext* ctx,
    buffer_handle_t full_frame_buffer,
    uint32_t frame_number) {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());

#if USE_CAMERA_FEATURE_AUTO_FRAMING
  if (ctx->state_transition.first != State::kAutoFramingOff &&
      ctx->state_transition.second == State::kAutoFramingOff) {
    if (!auto_framing_client_.ResetCropWindow(*ctx->timestamp)) {
      LOGF(ERROR) << "Failed to reset crop window at result " << frame_number;
      return false;
    }
  }
  if (ctx->state_transition.first != State::kAutoFramingOn &&
      ctx->state_transition.second == State::kAutoFramingOn) {
    auto_framing_client_.ResetDetectionTimer();
  }
  if (!auto_framing_client_.ProcessFrame(
          *ctx->timestamp, ctx->state_transition.second == State::kAutoFramingOn
                               ? full_frame_buffer
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
  return true;
#else
  // This function should not be called if there is no auto framing.
  return false;
#endif  // USE_CAMERA_FEATURE_AUTO_FRAMING
}

bool FramingStreamManipulator::SetUpPipelineOnThread(
    uint32_t target_aspect_ratio_x, uint32_t target_aspect_ratio_y) {
#if USE_CAMERA_FEATURE_AUTO_FRAMING
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_AUTO_FRAMING();

  return auto_framing_client_.SetUp(AutoFramingClient::Options{
      .input_size = full_frame_size_,
      .frame_rate = static_cast<double>(kRequiredVideoFrameRate),
      .target_aspect_ratio_x = target_aspect_ratio_x,
      .target_aspect_ratio_y = target_aspect_ratio_y,
      .detection_rate = options_.detection_rate,
  });
#else
  return true;
#endif  // USE_CAMERA_FEATURE_AUTO_FRAMING
}

void FramingStreamManipulator::UpdateFaceRectangleMetadataOnThread(
    Camera3CaptureDescriptor* result) {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_AUTO_FRAMING();

  if (!result->has_metadata()) {
    return;
  }

  // |roi_in_active_array| and |crop_in_active_array| are in normalized
  // coordinates.
  const Rect<float> roi_in_active_array =
      ConvertToParentSpace(region_of_interest_, full_frame_crop_);
  const Rect<float> crop_in_active_array =
      ConvertToParentSpace(active_crop_region_, full_frame_crop_);

  auto normalized_rect_to_active_array_rect =
      [&](Rect<float> normalized_rect) -> Rect<float> {
    return Rect<float>(normalized_rect.left *
                           static_cast<float>(active_array_dimension_.width),
                       normalized_rect.top *
                           static_cast<float>(active_array_dimension_.height),
                       normalized_rect.width *
                           static_cast<float>(active_array_dimension_.width),
                       normalized_rect.height *
                           static_cast<float>(active_array_dimension_.height));
  };

  auto convert_active_array_rect_to_crop_space =
      [&](float x1, float y1, float x2,
          float y2) -> std::optional<Rect<float>> {
    const Rect<float> raw_rect(x1, y1, std::max(x2 - x1, 1.0f),
                               std::max(y2 - y1, 1.0f));
    const Rect<float> clamped_rect = ClampRect(
        raw_rect,
        Rect<float>(0.0f, 0.0f,
                    static_cast<float>(active_array_dimension_.width),
                    static_cast<float>(active_array_dimension_.height)));
    if (!clamped_rect.is_valid()) {
      VLOGF(1) << "Invalid face rectangle: " << raw_rect.ToString();
      return std::nullopt;
    }
    return normalized_rect_to_active_array_rect(
        ConvertToCropSpace(NormalizeRect(clamped_rect, active_array_dimension_),
                           crop_in_active_array));
  };

  // |face_rectangles| stores face bounding boxes in active array coordinates.
  std::vector<Rect<float>> face_rectangles;
  if (options_.debug) {
    // Show the detected faces, aggregated region of interest and the active
    // crop region in debug mode.
    face_rectangles = faces_;
    face_rectangles.push_back(
        normalized_rect_to_active_array_rect(roi_in_active_array));
    face_rectangles.push_back(
        normalized_rect_to_active_array_rect(crop_in_active_array));
    if (!result->UpdateMetadata<uint8_t>(
            ANDROID_STATISTICS_FACE_DETECT_MODE,
            (uint8_t[]){ANDROID_STATISTICS_FACE_DETECT_MODE_SIMPLE})) {
      LOGF(ERROR) << "Cannot set ANDROID_STATISTICS_FACE_DETECT_MODE";
    }
  } else {
    // By default translate the face rectangles in the result metadata to
    // the crop coordinate space.
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
      std::optional<Rect<float>> converted_rect =
          convert_active_array_rect_to_crop_space(
              static_cast<float>(rect_bound[0]),
              static_cast<float>(rect_bound[1]),
              static_cast<float>(rect_bound[2]),
              static_cast<float>(rect_bound[3]));
      if (!converted_rect) {
        continue;
      }
      face_rectangles.push_back(converted_rect.value());
    }

    // Convert the coordinates for feature metadata provided by the
    // FaceDetectionStreamManipulator.
    if (result->feature_metadata().faces) {
      for (auto& f : result->feature_metadata().faces.value()) {
        std::optional<Rect<float>> converted_box =
            convert_active_array_rect_to_crop_space(
                f.bounding_box.x1, f.bounding_box.y1, f.bounding_box.x2,
                f.bounding_box.y2);
        if (!converted_box) {
          continue;
        }
        f.bounding_box = {.x1 = converted_box->left,
                          .y1 = converted_box->top,
                          .x2 = converted_box->right(),
                          .y2 = converted_box->bottom()};
        for (auto& l : f.landmarks) {
          std::optional<Rect<float>> converted_landmark =
              convert_active_array_rect_to_crop_space(l.x, l.y, l.x, l.y);
          if (!converted_landmark) {
            continue;
          }
          l.x = converted_landmark->left;
          l.y = converted_landmark->top;
        }
      }
    }
  }

  // Update the face rectangles metadata passed to the camera clients.
  std::vector<int32_t> face_coordinates;
  for (const auto& f : face_rectangles) {
    face_coordinates.push_back(static_cast<int32_t>(f.left));
    face_coordinates.push_back(static_cast<int32_t>(f.top));
    face_coordinates.push_back(static_cast<int32_t>(f.right()));
    face_coordinates.push_back(static_cast<int32_t>(f.bottom()));
  }
  if (!face_coordinates.empty() &&
      !result->UpdateMetadata<int32_t>(ANDROID_STATISTICS_FACE_RECTANGLES,
                                       face_coordinates)) {
    LOGF(ERROR) << "Cannot set ANDROID_STATISTICS_FACE_RECTANGLES";
  }
}

void FramingStreamManipulator::ResetOnThread() {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_AUTO_FRAMING();

  UploadMetricsOnThread();

#if USE_CAMERA_FEATURE_AUTO_FRAMING
  auto_framing_client_.TearDown();
#endif  // USE_CAMERA_FEATURE_AUTO_FRAMING

  still_capture_processor_->Reset();

  state_ = State::kDisabled;
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

  metrics_ = Metrics{};
}

void FramingStreamManipulator::UploadMetricsOnThread() {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());

#if USE_CAMERA_FEATURE_AUTO_FRAMING
  const AutoFramingClient::Metrics pipeline_metrics =
      auto_framing_client_.GetMetrics();
  // Skip sessions that no frames are actually captured.
  if (metrics_.num_captures == 0) {
    return;
  }
  VLOGF(1) << "Metrics:" << " num_captures=" << metrics_.num_captures
           << " enabled_count=" << metrics_.enabled_count
           << " accumulated_on_time=" << metrics_.accumulated_on_time
           << " accumulated_off_time=" << metrics_.accumulated_off_time
           << " num_detections=" << pipeline_metrics.num_detections
           << " num_detection_hits=" << pipeline_metrics.num_detection_hits
           << " accumulated_detection_latency="
           << pipeline_metrics.accumulated_detection_latency;

  constexpr base::TimeDelta kRecordThreshold = base::Seconds(10);
  if (metrics_.accumulated_on_time + metrics_.accumulated_off_time >=
      kRecordThreshold) {
    camera_metrics_->SendAutoFramingEnabledTimePercentage(static_cast<int>(
        metrics_.accumulated_on_time /
        (metrics_.accumulated_on_time + metrics_.accumulated_off_time) *
        100.0f));
  }
  camera_metrics_->SendAutoFramingEnabledCount(metrics_.enabled_count);

  if (pipeline_metrics.num_detections > 0) {
    const int detection_hit_rate = pipeline_metrics.num_detection_hits * 100 /
                                   pipeline_metrics.num_detections;
    const base::TimeDelta avg_detection_latency =
        pipeline_metrics.accumulated_detection_latency /
        pipeline_metrics.num_detections;
    VLOGF(1) << "Detection hit rate: " << detection_hit_rate << "%";
    VLOGF(1) << "Average detection latency: " << avg_detection_latency;
    camera_metrics_->SendAutoFramingDetectionHitPercentage(detection_hit_rate);
    camera_metrics_->SendAutoFramingAvgDetectionLatency(avg_detection_latency);
  }
  if (pipeline_metrics.zoom_ratio_tenths_histogram.size() > 0) {
    const int median_zoom_ratio_tenths =
        CalculateMedian(pipeline_metrics.zoom_ratio_tenths_histogram);
    VLOGF(1) << "Median zoom ratio: "
             << static_cast<float>(median_zoom_ratio_tenths) / 10.0f;
    camera_metrics_->SendAutoFramingMedianZoomRatio(median_zoom_ratio_tenths);
  }

  bool has_error = false;
  for (auto& errors : {metrics_.errors, pipeline_metrics.errors}) {
    for (auto [error, count] : errors) {
      if (count > 0) {
        // Only report each error once in a session.
        LOGF(ERROR) << "There were " << count << " occurrences of error "
                    << static_cast<int>(error);
        camera_metrics_->SendAutoFramingError(error);
        has_error = true;
      }
    }
  }
  if (!has_error) {
    camera_metrics_->SendAutoFramingError(AutoFramingError::kNoError);
  }
#endif  // USE_CAMERA_FEATURE_AUTO_FRAMING
}

void FramingStreamManipulator::UpdateOptionsOnThread(
    const base::Value::Dict& json_values) {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());

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
  options_.enable = json_values.FindBool(kEnableKey);
  LoadIfExist(json_values, kDebugKey, &options_.debug);

  VLOGF(1) << "AutoFramingStreamManipulator options:" << " max_video_width="
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

void FramingStreamManipulator::OnOptionsUpdated(
    const base::Value::Dict& json_values) {
  gpu_resources_->PostGpuTask(
      FROM_HERE,
      base::BindOnce(&FramingStreamManipulator::UpdateOptionsOnThread,
                     base::Unretained(this), json_values.Clone()));
}

std::pair<FramingStreamManipulator::State, FramingStreamManipulator::State>
FramingStreamManipulator::StateTransitionOnThread(bool manual_zoom_enabled) {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  TRACE_AUTO_FRAMING();

  // Auto-framing state transition graph:
  //
  //     d--> kDisabled --a    b-----------------------|
  //     |                v    |                       v
  //   kOff --a--> kTransitionToOn --c--> kOn --b--> kTransitionToOff
  //     ^                     ^                       |  |
  //     |                     |-----------------------a  |
  //     |------------------------------------------------c
  //
  //   a. Runtime switch changed to ON
  //   b. Runtime switch changed to OFF
  //   c. Enabling/Disabling delay time reached
  //   d. Crop window stays at full region
  //
  // Note that crop window can be moving in |kOff| state, but needs to be fixed
  // at full region in |kDisabled| state. For d, we check whether
  // |active_crop_region_| is full, which is used in the last capture result. If
  // it is, we assume it stays full up to the time this state change is applied
  // in the future capture result.
  //
  // Manual zoom state is toggled according to |manual_zoom_enabled|, given the
  // current state is not auto-framing states. In case auto framing and manual
  // zoom are activated at the same capture request, auto framing is activated.

  const State prev_state = state_;
  bool auto_framing_enabled = GetAutoFramingEnabled();

  // Auto framing states
  if (auto_framing_enabled) {
    if (state_ == State::kDisabled || state_ == State::kAutoFramingOff ||
        state_ == State::kTransitionToAutoFramingOff) {
      state_ = State::kTransitionToAutoFramingOn;
    } else if (state_ == State::kTransitionToAutoFramingOn &&
               state_transition_timer_.Elapsed() >= options_.enable_delay) {
      state_ = State::kAutoFramingOn;
    }
  } else {
    if (state_ == State::kAutoFramingOn ||
        state_ == State::kTransitionToAutoFramingOn) {
      state_ = State::kTransitionToAutoFramingOff;
    } else if (state_ == State::kTransitionToAutoFramingOff &&
               state_transition_timer_.Elapsed() >= options_.disable_delay) {
      state_ = State::kAutoFramingOff;
    } else if (state_ == State::kAutoFramingOff &&
               IsFullCrop(active_crop_region_)) {
      state_ = State::kDisabled;
    }
  }

  // Manual zoom states
  if (prev_state == State::kDisabled && manual_zoom_enabled &&
      !auto_framing_enabled) {
    state_ = State::kManualZoom;
  } else if (prev_state == State::kManualZoom && !manual_zoom_enabled) {
    state_ = State::kDisabled;
  }

  // Collect metrics when the state is updated.
  if (prev_state != state_) {
    LOGF(INFO) << "State: " << static_cast<int>(prev_state) << " -> "
               << static_cast<int>(state_);
    if (prev_state == State::kAutoFramingOn) {
      metrics_.accumulated_on_time += state_transition_timer_.Elapsed();
    } else if ((prev_state == State::kDisabled &&
                state_ != State::kManualZoom) ||
               prev_state == State::kAutoFramingOff) {
      metrics_.accumulated_off_time += state_transition_timer_.Elapsed();
    }
    if (state_ == State::kAutoFramingOn) {
      ++metrics_.enabled_count;
    }
    state_transition_timer_ = base::ElapsedTimer();
  }
  return std::make_pair(prev_state, state_);
}

FramingStreamManipulator::CaptureContext*
FramingStreamManipulator::CreateCaptureContext(uint32_t frame_number) {
  CHECK(!base::Contains(capture_contexts_, frame_number));
  auto [it, is_inserted] = capture_contexts_.insert(
      std::make_pair(frame_number, std::make_unique<CaptureContext>()));
  if (!is_inserted) {
    LOGF(ERROR) << "Multiple captures with same frame number " << frame_number;
    return nullptr;
  }
  return it->second.get();
}

FramingStreamManipulator::CaptureContext*
FramingStreamManipulator::GetCaptureContext(uint32_t frame_number) const {
  auto it = capture_contexts_.find(frame_number);
  return it != capture_contexts_.end() ? it->second.get() : nullptr;
}

void FramingStreamManipulator::RemoveCaptureContext(uint32_t frame_number) {
  capture_contexts_.erase(frame_number);
}

std::optional<base::ScopedFD> FramingStreamManipulator::CropAndScaleOnThread(
    buffer_handle_t input_yuv,
    base::ScopedFD input_release_fence,
    buffer_handle_t output_yuv,
    base::ScopedFD output_acquire_fence,
    const Rect<float>& crop_region,
    ScaleMethod method) {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());
  CHECK_NE(method, ScaleMethod::kInvalid);
  TRACE_AUTO_FRAMING();

  if (input_release_fence.is_valid() &&
      sync_wait(input_release_fence.get(), kSyncWaitTimeoutMs) != 0) {
    LOGF(ERROR) << "sync_wait() timed out on input buffer";
    return std::nullopt;
  }
  if (output_acquire_fence.is_valid() &&
      sync_wait(output_acquire_fence.get(), kSyncWaitTimeoutMs) != 0) {
    LOGF(ERROR) << "sync_wait() timed out on output buffer";
    return std::nullopt;
  }

  // Allocate a buffer to hold the cropped yuv for still capture upsampling.
  bool is_upsample_request = false;
  ScopedBufferHandle upsample_input_buffer;
#if USE_CAMERA_FEATURE_SUPER_RES
  Size dimension(CameraBufferManager::GetWidth(input_yuv),
                 CameraBufferManager::GetHeight(input_yuv));
  is_upsample_request =
      method != ScaleMethod::kBicubic && single_frame_upsampler_ &&
      IsUpsampleRequestValid(CameraBufferManager::GetWidth(output_yuv),
                             CameraBufferManager::GetHeight(output_yuv),
                             crop_region, dimension);

  if (is_upsample_request) {
    auto [crop_width, crop_height] =
        GetEvenInputDimensions(crop_region, dimension);
    upsample_input_buffer = CameraBufferManager::AllocateScopedBuffer(
        crop_width, crop_height, HAL_PIXEL_FORMAT_YCbCr_420_888,
        kStillYuvBufferUsage);
  }
#endif  // USE_CAMERA_FEATURE_SUPER_RES

  SharedImage input_image = SharedImage::CreateFromBuffer(
      input_yuv, Texture2D::Target::kTarget2D, true);
  if (!input_image.IsValid()) {
    LOGF(ERROR) << "Failed to create shared image from input buffer";
    return std::nullopt;
  }
  SharedImage output_image = SharedImage::CreateFromBuffer(
      is_upsample_request ? *upsample_input_buffer : output_yuv,
      Texture2D::Target::kTarget2D, true);
  if (!output_image.IsValid()) {
    LOGF(ERROR) << "Failed to create shared image from output buffer";
    return std::nullopt;
  }

  gpu_resources_->image_processor()->CropYuv(
      input_image.y_texture(), input_image.uv_texture(), crop_region,
      output_image.y_texture(), output_image.uv_texture(),
      options_.output_filter_mode);

#if USE_CAMERA_FEATURE_SUPER_RES
  // Perform upsampling on the cropped yuv buffer for still capture.
  if (is_upsample_request) {
    std::optional<base::ScopedFD> upsample_fence =
        single_frame_upsampler_->ProcessRequest(
            *upsample_input_buffer, output_yuv, EglFence().GetNativeFd(),
            ScaleMethodToResamplingMethod(method),
            /*use_lancet_alpha=*/method == ScaleMethod::kLancetAlpha ? true
                                                                     : false);
    if (!upsample_fence.has_value()) {
      LOGF(ERROR) << "Failed to upsample from cropped buffer";
      return std::nullopt;
    }
    return upsample_fence;
  }
#endif  // USE_CAMERA_FEATURE_SUPER_RES

  EglFence fence;
  return fence.GetNativeFd();
}

#if USE_CAMERA_FEATURE_SUPER_RES
void FramingStreamManipulator::ProduceMultiUpsamplingResults(
    int64_t timestamp,
    buffer_handle_t full_yuv,
    const Rect<float>& crop_region) {
  DCHECK(gpu_resources_->gpu_task_runner()->BelongsToCurrentThread());

  const base::FilePath dir_path("/usr/local/super_res/");
  std::string timestamp_str = std::to_string(timestamp);
  uint32_t output_width = CameraBufferManager::GetWidth(full_yuv);
  uint32_t output_height = CameraBufferManager::GetHeight(full_yuv);

  // *** File Structure Explanation ***
  // Images will be stored in the following directory structure:
  // * input_full/: Contains the original, uncropped YUV frames.
  // * input_cropped/: Contains Bicubic cropped YUV frames.
  // * (ScaleMethod)/: Contains upsampled YUV frames.

  // Store original (full) yuv frame.
  std::string folder = "input_full/";
  if (!base::CreateDirectory(dir_path.Append(folder))) {
    LOG(ERROR) << "Failed to create directory " << dir_path;
  }
  std::string filename =
      folder + base::StringPrintf("%dx%d_result_%s.yuv", output_width,
                                  output_height, timestamp_str.c_str());
  if (!WriteBufferIntoFile(full_yuv, dir_path.Append(filename))) {
    LOGF(ERROR) << "Failed to dump full YUV buffer";
  }

  // Store cropped yuv from the original frame.
  Size dimension(CameraBufferManager::GetWidth(full_yuv),
                 CameraBufferManager::GetHeight(full_yuv));
  auto [crop_width, crop_height] =
      GetEvenInputDimensions(crop_region, dimension);
  ScopedBufferHandle cropped_buf = CameraBufferManager::AllocateScopedBuffer(
      crop_width, crop_height, HAL_PIXEL_FORMAT_YCbCr_420_888,
      kStillYuvBufferUsage);
  std::optional<base::ScopedFD> fence = CropAndScaleOnThread(
      full_yuv, base::ScopedFD(), *cropped_buf, base::ScopedFD(), crop_region,
      ScaleMethod::kBicubic);
  CHECK(fence.has_value());
  if (fence->is_valid() && sync_wait(fence->get(), kSyncWaitTimeoutMs) != 0) {
    LOGF(ERROR) << "sync_wait() timed out on buffer with "
                << ScaleMethodToString(ScaleMethod::kBicubic);
  } else {
    folder = "input_cropped/";
    if (!base::CreateDirectory(dir_path.Append(folder))) {
      LOG(ERROR) << "Failed to create directory " << dir_path;
    }
    filename = folder +
               base::StringPrintf("%dx%d_result_%s.yuv",
                                  CameraBufferManager::GetWidth(*cropped_buf),
                                  CameraBufferManager::GetHeight(*cropped_buf),
                                  timestamp_str.c_str());
    if (!WriteBufferIntoFile(*cropped_buf, dir_path.Append(filename))) {
      LOGF(ERROR) << "Failed to dump cropped YUV buffer";
    }
  }

  // Store upsampled YUV frames (using different upsampling methods).
  ScopedBufferHandle out_buf;
  const int kNumOfScaleMethods = 5;
  for (int i = 0; i < kNumOfScaleMethods; i++) {
    out_buf = CameraBufferManager::AllocateScopedBuffer(
        output_width, output_height, HAL_PIXEL_FORMAT_YCbCr_420_888,
        kStillYuvBufferUsage);
    fence = CropAndScaleOnThread(full_yuv, base::ScopedFD(), *out_buf,
                                 base::ScopedFD(), crop_region,
                                 static_cast<ScaleMethod>(i));
    CHECK(fence.has_value());
    if (fence->is_valid() && sync_wait(fence->get(), kSyncWaitTimeoutMs) != 0) {
      LOGF(ERROR) << "sync_wait() timed out on buffer with "
                  << ScaleMethodToString(static_cast<ScaleMethod>(i));
    } else {
      folder = ScaleMethodToString(static_cast<ScaleMethod>(i)) + "/";
      if (!base::CreateDirectory(dir_path.Append(folder))) {
        LOG(ERROR) << "Failed to create directory " << dir_path;
      }
      filename =
          folder + base::StringPrintf("%dx%d_result_%s.yuv", output_width,
                                      output_height, timestamp_str.c_str());
      if (!WriteBufferIntoFile(*out_buf, dir_path.Append(filename))) {
        LOGF(ERROR) << "Failed to dump upsampled " +
                           ScaleMethodToString(static_cast<ScaleMethod>(i)) +
                           " YUV buffer";
      }
    }
  }
}
#endif  // USE_CAMERA_FEATURE_SUPER_RES

}  // namespace cros
