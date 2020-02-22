/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/usb/metadata_handler.h"

#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

#include "cros-camera/common.h"
#include "cros-camera/utils/camera_config.h"
#include "hal/usb/quirks.h"
#include "hal/usb/stream_format.h"
#include "hal/usb/v4l2_camera_device.h"
#include "hal/usb/vendor_tag.h"
#include "mojo/cros_camera_enum.mojom.h"

namespace {

constexpr int32_t kMinFps = 1;

class MetadataUpdater {
 public:
  explicit MetadataUpdater(android::CameraMetadata* metadata)
      : metadata_(metadata), ok_(true) {}

  bool ok() { return ok_; }

  template <typename T>
  void operator()(int tag, const std::vector<T>& data) {
    if (!ok_) {
      return;
    }
    if (metadata_->update(tag, data) != 0) {
      ok_ = false;
      LOGF(ERROR) << "Update metadata with tag " << tag << " failed";
    }
  }

  template <typename T>
  std::enable_if_t<std::is_enum<T>::value> operator()(int tag, const T& data) {
    static const std::set<int> kInt32EnumTags = {
        ANDROID_DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS,
        ANDROID_SCALER_AVAILABLE_FORMATS,
        ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
        ANDROID_SENSOR_TEST_PATTERN_MODE,
        ANDROID_SYNC_MAX_LATENCY,
    };
    if (kInt32EnumTags.find(tag) != kInt32EnumTags.end()) {
      operator()(tag, std::vector<int32_t>{static_cast<int32_t>(data)});
    } else {
      operator()(tag, std::vector<uint8_t>{base::checked_cast<uint8_t>(data)});
    }
  }

  template <typename T>
  std::enable_if_t<!std::is_enum<T>::value> operator()(int tag, const T& data) {
    operator()(tag, std::vector<T>{data});
  }

 private:
  android::CameraMetadata* metadata_;
  bool ok_;
};

}  // namespace

namespace cros {

MetadataHandler::MetadataHandler(const camera_metadata_t& static_metadata,
                                 const camera_metadata_t& request_template,
                                 const DeviceInfo& device_info,
                                 V4L2CameraDevice* device,
                                 const SupportedFormats& supported_formats)
    : device_info_(device_info), device_(device), af_trigger_(false) {
  // MetadataBase::operator= will make a copy of camera_metadata_t.
  static_metadata_ = &static_metadata;
  request_template_ = &request_template;

  max_supported_fps_ = 0;
  for (const auto& format : supported_formats) {
    for (const float& frame_rate : format.frame_rates) {
      // Since the |frame_rate| is a float, we need to round here.
      int fps = std::round(frame_rate);
      if (fps > max_supported_fps_) {
        max_supported_fps_ = fps;
      }
    }
  }

  // camera3_request_template_t starts at 1.
  for (int i = 1; i < CAMERA3_TEMPLATE_COUNT; i++) {
    template_settings_[i] = CreateDefaultRequestSettings(i);
  }

  sensor_handler_ = SensorHandler::Create(device_info, supported_formats);

  thread_checker_.DetachFromThread();
}

MetadataHandler::~MetadataHandler() {}

int MetadataHandler::FillDefaultMetadata(
    android::CameraMetadata* static_metadata,
    android::CameraMetadata* request_metadata) {
  MetadataUpdater update_static(static_metadata);
  MetadataUpdater update_request(request_metadata);

  // android.colorCorrection
  update_static(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
                std::vector<uint8_t>{
                    ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST,
                    ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY});
  update_request(ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
                 ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST);

  // android.control
  // We don't support AE compensation.
  update_static(ANDROID_CONTROL_AE_COMPENSATION_RANGE,
                std::vector<int32_t>{0, 0});

  update_static(ANDROID_CONTROL_AE_COMPENSATION_STEP,
                camera_metadata_rational_t{0, 1});

  update_static(ANDROID_CONTROL_MAX_REGIONS,
                std::vector<int32_t>{/*AE*/ 0, /*AWB*/ 0, /*AF*/ 0});

  update_static(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
                ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF);
  update_request(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
                 ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF);

  update_static(ANDROID_CONTROL_AWB_AVAILABLE_MODES,
                ANDROID_CONTROL_AWB_MODE_AUTO);
  update_request(ANDROID_CONTROL_AWB_MODE, ANDROID_CONTROL_AWB_MODE_AUTO);

  update_static(ANDROID_CONTROL_AE_AVAILABLE_MODES, ANDROID_CONTROL_AE_MODE_ON);
  // ON means auto-exposure is active with no flash control.
  update_request(ANDROID_CONTROL_AE_MODE, ANDROID_CONTROL_AE_MODE_ON);

  update_request(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, int32_t{0});

  update_request(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER,
                 ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE);

  update_request(ANDROID_CONTROL_AF_TRIGGER, ANDROID_CONTROL_AF_TRIGGER_IDLE);

  update_static(ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
                ANDROID_CONTROL_SCENE_MODE_DISABLED);
  update_request(ANDROID_CONTROL_SCENE_MODE,
                 ANDROID_CONTROL_SCENE_MODE_DISABLED);

  update_static(ANDROID_CONTROL_AVAILABLE_EFFECTS,
                ANDROID_CONTROL_EFFECT_MODE_OFF);
  update_request(ANDROID_CONTROL_EFFECT_MODE, ANDROID_CONTROL_EFFECT_MODE_OFF);

  update_static(ANDROID_CONTROL_AE_LOCK_AVAILABLE,
                ANDROID_CONTROL_AE_LOCK_AVAILABLE_FALSE);

  update_static(ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
                ANDROID_CONTROL_AWB_LOCK_AVAILABLE_FALSE);

  update_static(ANDROID_CONTROL_AVAILABLE_MODES,
                std::vector<uint8_t>{ANDROID_CONTROL_MODE_OFF,
                                     ANDROID_CONTROL_MODE_AUTO});

  // android.flash
  update_static(ANDROID_FLASH_INFO_AVAILABLE,
                ANDROID_FLASH_INFO_AVAILABLE_FALSE);
  update_request(ANDROID_FLASH_STATE, ANDROID_FLASH_STATE_UNAVAILABLE);
  update_request(ANDROID_FLASH_MODE, ANDROID_FLASH_MODE_OFF);

  // android.jpeg
  update_static(ANDROID_JPEG_MAX_SIZE, int32_t{13 << 20});
  update_request(ANDROID_JPEG_QUALITY, uint8_t{90});
  update_request(ANDROID_JPEG_THUMBNAIL_QUALITY, uint8_t{90});
  update_request(ANDROID_JPEG_ORIENTATION, int32_t{0});

  // android.lens
  // This should not be needed.
  update_static(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE, 0.0f);
  update_static(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
                ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF);
  update_request(ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
                 ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF);

  // android.noiseReduction
  update_static(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
                ANDROID_NOISE_REDUCTION_MODE_OFF);
  update_request(ANDROID_NOISE_REDUCTION_MODE,
                 ANDROID_NOISE_REDUCTION_MODE_OFF);

  // android.request
  update_static(ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE);
  update_static(ANDROID_REQUEST_PARTIAL_RESULT_COUNT, int32_t{1});

  // This means pipeline latency of X frame intervals. The maximum number is 4.
  update_static(ANDROID_REQUEST_PIPELINE_MAX_DEPTH, uint8_t{4});
  update_request(ANDROID_REQUEST_PIPELINE_DEPTH, uint8_t{4});

  // Three numbers represent the maximum numbers of different types of output
  // streams simultaneously. The types are raw sensor, processed (but not
  // stalling), and processed (but stalling). For usb limited mode, raw sensor
  // is not supported. Stalling stream is JPEG. Non-stalling streams are
  // YUV_420_888, NV21, or YV12.
  update_static(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
                std::vector<int32_t>{0, 2, 1});

  // Limited mode doesn't support reprocessing.
  update_static(ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS, int32_t{0});

  // android.scaler
  update_static(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, 1.0f);

  update_static(ANDROID_SCALER_CROPPING_TYPE,
                ANDROID_SCALER_CROPPING_TYPE_CENTER_ONLY);

  update_static(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,
                std::vector<int32_t>{
                    ANDROID_SENSOR_TEST_PATTERN_MODE_OFF,
                    ANDROID_SENSOR_TEST_PATTERN_MODE_COLOR_BARS_FADE_TO_GRAY});
  update_request(ANDROID_SENSOR_TEST_PATTERN_MODE,
                 ANDROID_SENSOR_TEST_PATTERN_MODE_OFF);

  uint8_t timestamp_source;
  if (V4L2CameraDevice::GetUvcClock() == CLOCK_BOOTTIME) {
    timestamp_source = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME;
  } else {
    timestamp_source = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN;
  }
  update_static(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, timestamp_source);

  // android.shading
  update_static(ANDROID_SHADING_AVAILABLE_MODES, ANDROID_SHADING_MODE_FAST);

  // android.statistics
  update_static(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
                ANDROID_STATISTICS_FACE_DETECT_MODE_OFF);
  update_request(ANDROID_STATISTICS_FACE_DETECT_MODE,
                 ANDROID_STATISTICS_FACE_DETECT_MODE_OFF);

  update_static(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, int32_t{0});

  update_static(ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
                ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF);
  update_request(ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE,
                 ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF);

  update_static(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
                ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF);

  // android.sync
  update_static(ANDROID_SYNC_MAX_LATENCY, ANDROID_SYNC_MAX_LATENCY_UNKNOWN);

  return update_static.ok() && update_request.ok() ? 0 : -EINVAL;
}

int MetadataHandler::FillMetadataFromSupportedFormats(
    const SupportedFormats& supported_formats,
    const DeviceInfo& device_info,
    android::CameraMetadata* static_metadata,
    android::CameraMetadata* request_metadata) {
  bool is_external = device_info.lens_facing == ANDROID_LENS_FACING_EXTERNAL;

  if (supported_formats.empty()) {
    return -EINVAL;
  }
  std::vector<int32_t> stream_configurations;
  std::vector<int64_t> min_frame_durations;
  std::vector<int64_t> stall_durations;

  // The min fps <= 15 must be supported in CTS.
  const int64_t kOneSecOfNanoUnit = 1000000000LL;
  int32_t max_fps = std::numeric_limits<int32_t>::min();
  int64_t max_frame_duration = kOneSecOfNanoUnit / kMinFps;
  std::set<int32_t> supported_fps;

  std::vector<int> hal_formats{HAL_PIXEL_FORMAT_BLOB,
                               HAL_PIXEL_FORMAT_YCbCr_420_888,
                               HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED};

  std::unordered_map<int, int> max_hal_width_by_format;
  std::unordered_map<int, int> max_hal_height_by_format;
  std::unique_ptr<CameraConfig> camera_config =
      CameraConfig::Create(constants::kCrosCameraConfigPathString);
  max_hal_width_by_format[HAL_PIXEL_FORMAT_BLOB] = camera_config->GetInteger(
      constants::kCrosMaxBlobWidth, std::numeric_limits<int>::max());
  max_hal_width_by_format[HAL_PIXEL_FORMAT_YCbCr_420_888] =
      camera_config->GetInteger(constants::kCrosMaxYuvWidth,
                                std::numeric_limits<int>::max());
  max_hal_width_by_format[HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED] =
      camera_config->GetInteger(constants::kCrosMaxPrivateWidth,
                                std::numeric_limits<int>::max());

  max_hal_height_by_format[HAL_PIXEL_FORMAT_BLOB] = camera_config->GetInteger(
      constants::kCrosMaxBlobHeight, std::numeric_limits<int>::max());
  max_hal_height_by_format[HAL_PIXEL_FORMAT_YCbCr_420_888] =
      camera_config->GetInteger(constants::kCrosMaxYuvHeight,
                                std::numeric_limits<int>::max());
  max_hal_height_by_format[HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED] =
      camera_config->GetInteger(constants::kCrosMaxPrivateHeight,
                                std::numeric_limits<int>::max());

  for (const auto& supported_format : supported_formats) {
    int64_t min_frame_duration = std::numeric_limits<int64_t>::max();
    int32_t per_format_max_fps = std::numeric_limits<int32_t>::min();
    for (const auto& frame_rate : supported_format.frame_rates) {
      // To prevent floating point precision problem we cast the floating point
      // to double here.
      int64_t frame_duration =
          kOneSecOfNanoUnit / static_cast<double>(frame_rate);
      if (frame_duration < min_frame_duration) {
        min_frame_duration = frame_duration;
      }
      if (frame_duration > max_frame_duration) {
        max_frame_duration = frame_duration;
      }
      if (per_format_max_fps < static_cast<int32_t>(frame_rate)) {
        per_format_max_fps = static_cast<int32_t>(frame_rate);
      }
      supported_fps.insert(frame_rate);
    }
    if (per_format_max_fps > max_fps) {
      max_fps = per_format_max_fps;
    }

    for (const auto& format : hal_formats) {
      if (!is_external) {
        if (supported_format.width > max_hal_width_by_format[format]) {
          LOGF(INFO) << "Filter Format: 0x" << std::hex << format << std::dec
                     << "-width " << supported_format.width << ". max is "
                     << max_hal_width_by_format[format];
          continue;
        }
        if (supported_format.height > max_hal_height_by_format[format]) {
          LOGF(INFO) << "Filter Format: 0x" << std::hex << format << std::dec
                     << "-height " << supported_format.height << ". max is "
                     << max_hal_height_by_format[format];
          continue;
        }
        if (format != HAL_PIXEL_FORMAT_BLOB && per_format_max_fps < 30) {
          continue;
        }
      }

      stream_configurations.push_back(format);
      stream_configurations.push_back(supported_format.width);
      stream_configurations.push_back(supported_format.height);
      stream_configurations.push_back(
          ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT);

      min_frame_durations.push_back(format);
      min_frame_durations.push_back(supported_format.width);
      min_frame_durations.push_back(supported_format.height);
      min_frame_durations.push_back(min_frame_duration);
    }

    // The stall duration is 0 for non-jpeg formats. For JPEG format, stall
    // duration can be 0 if JPEG is small. 5MP JPEG takes 700ms to decode
    // and encode. Here we choose 1 sec for JPEG.
    for (const auto& format : hal_formats) {
      // For non-jpeg formats, the camera orientation workaround crops,
      // rotates, and scales the frames. Theoretically the stall duration of
      // huge resolution may be bigger than 0. Set it to 0 for now.
      int64_t stall_duration =
          (format == HAL_PIXEL_FORMAT_BLOB) ? 1000000000 : 0;
      stall_durations.push_back(format);
      stall_durations.push_back(supported_format.width);
      stall_durations.push_back(supported_format.height);
      stall_durations.push_back(stall_duration);
    }
  }

  SupportedFormat maximum_format = GetMaximumFormat(supported_formats);
  std::vector<int32_t> active_array_size = {
      0, 0, static_cast<int32_t>(maximum_format.width),
      static_cast<int32_t>(maximum_format.height)};

  MetadataUpdater update_static(static_metadata);
  MetadataUpdater update_request(request_metadata);

  // The document in aeAvailableTargetFpsRanges section says the min fps should
  // not be larger than 15.
  // We enumerate all possible fps and put (min, fps) as available fps range. If
  // the device support constant frame rate, put (fps, fps) into the list as
  // well.
  // TODO(wtlee): Handle non-integer fps when setting controls.
  bool support_constant_framerate = !device_info.constant_framerate_unsupported;
  std::vector<int32_t> available_fps_ranges;

  // For devices that cannot actually meet the fps ranges they report, only
  // report (min, max) and optional (max, max) if they support constant frame
  // rate.
  if (device_info.quirks & kQuirkReportLeastFpsRanges) {
    available_fps_ranges.push_back(kMinFps);
    available_fps_ranges.push_back(max_fps);

    if (support_constant_framerate) {
      available_fps_ranges.push_back(max_fps);
      available_fps_ranges.push_back(max_fps);
    }
  } else {
    for (auto fps : supported_fps) {
      available_fps_ranges.push_back(kMinFps);
      available_fps_ranges.push_back(fps);

      if (support_constant_framerate) {
        available_fps_ranges.push_back(fps);
        available_fps_ranges.push_back(fps);
      }
    }
  }
  update_static(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
                available_fps_ranges);
  update_static(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                stream_configurations);
  update_static(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
                min_frame_durations);
  update_static(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS, stall_durations);

  std::vector<int32_t> jpeg_available_thumbnail_sizes =
      GetJpegAvailableThumbnailSizes(supported_formats);
  update_static(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
                jpeg_available_thumbnail_sizes);
  update_request(ANDROID_JPEG_THUMBNAIL_SIZE,
                 std::vector<int32_t>(jpeg_available_thumbnail_sizes.end() - 2,
                                      jpeg_available_thumbnail_sizes.end()));
  update_static(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION, max_frame_duration);
  update_static(ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
                active_array_size);
  update_static(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, active_array_size);

  if (is_external) {
    // It's a sensible value for external camera, since it's required on all
    // devices per spec.  For built-in camera, this would be filled in
    // FillMetadataFromDeviceInfo() using the value from the configuration file.
    // References:
    // * The official document for this field
    //   https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics.html#SENSOR_INFO_PIXEL_ARRAY_SIZE
    // * The implementation of external camera in Android P
    //   https://googleplex-android.git.corp.google.com/platform/hardware/interfaces/+/6ad8708bf8b631561fa11eb1f4889907d1772d78/camera/device/3.4/default/ExternalCameraDevice.cpp#687
    update_static(
        ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
        std::vector<int32_t>{static_cast<int32_t>(maximum_format.width),
                             static_cast<int32_t>(maximum_format.height)});
  }

  return update_static.ok() && update_request.ok() ? 0 : -EINVAL;
}

// static
int MetadataHandler::FillMetadataFromDeviceInfo(
    const DeviceInfo& device_info,
    android::CameraMetadata* static_metadata,
    android::CameraMetadata* request_metadata) {
  MetadataUpdater update_static(static_metadata);
  MetadataUpdater update_request(request_metadata);

  bool is_external = device_info.lens_facing == ANDROID_LENS_FACING_EXTERNAL;
  bool is_builtin = !is_external;

  std::vector<int32_t> available_request_keys = {
      ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
      ANDROID_CONTROL_AE_ANTIBANDING_MODE,
      ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
      ANDROID_CONTROL_AE_LOCK,
      ANDROID_CONTROL_AE_MODE,
      ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER,
      ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
      ANDROID_CONTROL_AF_MODE,
      ANDROID_CONTROL_AF_TRIGGER,
      ANDROID_CONTROL_AWB_LOCK,
      ANDROID_CONTROL_AWB_MODE,
      ANDROID_CONTROL_CAPTURE_INTENT,
      ANDROID_CONTROL_EFFECT_MODE,
      ANDROID_CONTROL_MODE,
      ANDROID_CONTROL_SCENE_MODE,
      ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
      ANDROID_FLASH_MODE,
      ANDROID_JPEG_ORIENTATION,
      ANDROID_JPEG_QUALITY,
      ANDROID_JPEG_THUMBNAIL_QUALITY,
      ANDROID_JPEG_THUMBNAIL_SIZE,
      ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
      ANDROID_NOISE_REDUCTION_MODE,
      ANDROID_SCALER_CROP_REGION,
      ANDROID_SENSOR_TEST_PATTERN_MODE,
      ANDROID_STATISTICS_FACE_DETECT_MODE,
      ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE,
  };
  if (is_builtin) {
    available_request_keys.insert(available_request_keys.end(),
                                  {
                                      ANDROID_LENS_APERTURE,
                                      ANDROID_LENS_FOCAL_LENGTH,
                                      ANDROID_LENS_FOCUS_DISTANCE,
                                  });
  }
  update_static(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, available_request_keys);

  // TODO(shik): All properties listed for capture requests can also be queried
  // on the capture result, to determine the final values used for capture. We
  // shuold build this list from |available_request_keys|.
  // ref:
  // https://developer.android.com/reference/android/hardware/camera2/CaptureResult
  std::vector<int32_t> available_result_keys = {
      ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
      ANDROID_CONTROL_AE_ANTIBANDING_MODE,
      ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION,
      ANDROID_CONTROL_AE_LOCK,
      ANDROID_CONTROL_AE_MODE,
      ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER,
      ANDROID_CONTROL_AE_STATE,
      ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
      ANDROID_CONTROL_AF_MODE,
      ANDROID_CONTROL_AF_STATE,
      ANDROID_CONTROL_AF_TRIGGER,
      ANDROID_CONTROL_AWB_LOCK,
      ANDROID_CONTROL_AWB_MODE,
      ANDROID_CONTROL_AWB_STATE,
      ANDROID_CONTROL_CAPTURE_INTENT,
      ANDROID_CONTROL_EFFECT_MODE,
      ANDROID_CONTROL_MODE,
      ANDROID_CONTROL_SCENE_MODE,
      ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
      ANDROID_FLASH_MODE,
      ANDROID_FLASH_STATE,
      ANDROID_JPEG_ORIENTATION,
      ANDROID_JPEG_QUALITY,
      ANDROID_JPEG_THUMBNAIL_QUALITY,
      ANDROID_JPEG_THUMBNAIL_SIZE,
      ANDROID_LENS_OPTICAL_STABILIZATION_MODE,
      ANDROID_LENS_STATE,
      ANDROID_NOISE_REDUCTION_MODE,
      ANDROID_REQUEST_PIPELINE_DEPTH,
      ANDROID_SCALER_CROP_REGION,
      ANDROID_SENSOR_ROLLING_SHUTTER_SKEW,
      ANDROID_SENSOR_TEST_PATTERN_MODE,
      ANDROID_SENSOR_TIMESTAMP,
      ANDROID_STATISTICS_FACE_DETECT_MODE,
      ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE,
      ANDROID_STATISTICS_LENS_SHADING_MAP_MODE,
      ANDROID_STATISTICS_SCENE_FLICKER,
  };
  if (is_builtin) {
    available_result_keys.insert(available_result_keys.end(),
                                 {
                                     ANDROID_LENS_APERTURE,
                                     ANDROID_LENS_FOCAL_LENGTH,
                                     ANDROID_LENS_FOCUS_DISTANCE,
                                 });
  }
  update_static(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, available_result_keys);

  // TODO(shik): The HAL must not have any tags in its static info that are not
  // listed either here or in the vendor tag list.  Some request/result metadata
  // entries are also presented in the static info now, and we should fix it.
  // ref:
  // https://android.googlesource.com/platform/system/media/+/a8cff157ff0ed02fa7e29438f4889a9933c37768/camera/docs/docs.html#16298
  std::vector<int32_t> available_characteristics_keys = {
      ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
      ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
      ANDROID_CONTROL_AE_AVAILABLE_MODES,
      ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES,
      ANDROID_CONTROL_AE_COMPENSATION_RANGE,
      ANDROID_CONTROL_AE_COMPENSATION_STEP,
      ANDROID_CONTROL_AE_LOCK_AVAILABLE,
      ANDROID_CONTROL_AF_AVAILABLE_MODES,
      ANDROID_CONTROL_AVAILABLE_EFFECTS,
      ANDROID_CONTROL_AVAILABLE_MODES,
      ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
      ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
      ANDROID_CONTROL_AWB_AVAILABLE_MODES,
      ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
      ANDROID_CONTROL_MAX_REGIONS,
      ANDROID_FLASH_INFO_AVAILABLE,
      ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
      ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
      ANDROID_JPEG_MAX_SIZE,
      ANDROID_LENS_FACING,
      ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
      ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
      ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE,
      ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
      ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
      ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS,
      ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS,
      ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
      ANDROID_REQUEST_PIPELINE_MAX_DEPTH,
      ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
      ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
      ANDROID_SCALER_CROPPING_TYPE,
      ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,
      ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
      ANDROID_SENSOR_INFO_MAX_FRAME_DURATION,
      ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
      ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
      ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
      ANDROID_SENSOR_ORIENTATION,
      ANDROID_SHADING_AVAILABLE_MODES,
      ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
      ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
      ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
      ANDROID_STATISTICS_INFO_MAX_FACE_COUNT,
      ANDROID_SYNC_MAX_LATENCY,
  };
  if (is_builtin) {
    available_characteristics_keys.insert(
        available_characteristics_keys.end(),
        {
            ANDROID_LENS_INFO_AVAILABLE_APERTURES,
            ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
            ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
            ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
        });
  }
  update_static(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
                available_characteristics_keys);

  update_static(ANDROID_SENSOR_ORIENTATION, device_info.sensor_orientation);
  update_static(ANDROID_LENS_FACING,
                static_cast<uint8_t>(device_info.lens_facing));

  if (is_builtin) {
    update_static(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
                  ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED);

    update_static(ANDROID_LENS_INFO_AVAILABLE_APERTURES,
                  device_info.lens_info_available_apertures);

    update_request(ANDROID_LENS_APERTURE,
                   device_info.lens_info_available_apertures[0]);

    update_static(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
                  device_info.lens_info_available_focal_lengths);

    update_request(ANDROID_LENS_FOCAL_LENGTH,
                   device_info.lens_info_available_focal_lengths[0]);

    update_static(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
                  device_info.lens_info_minimum_focus_distance);

    update_request(ANDROID_LENS_FOCUS_DISTANCE,
                   device_info.lens_info_optimal_focus_distance);

    update_static(
        ANDROID_SENSOR_INFO_PHYSICAL_SIZE,
        std::vector<float>{device_info.sensor_info_physical_size_width,
                           device_info.sensor_info_physical_size_height});

    update_static(
        ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE,
        std::vector<int32_t>{device_info.sensor_info_pixel_array_size_width,
                             device_info.sensor_info_pixel_array_size_height});
  } else {
    update_static(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
                  ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_EXTERNAL);
  }

  update_static(ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
                ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_UNCALIBRATED);

  update_static(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
                ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO);
  update_request(ANDROID_CONTROL_AE_ANTIBANDING_MODE,
                 ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO);

  bool support_af =
      V4L2CameraDevice::IsAutoFocusSupported(device_info.device_path);
  if (support_af) {
    update_static(ANDROID_CONTROL_AF_AVAILABLE_MODES,
                  std::vector<uint8_t>{ANDROID_CONTROL_AF_MODE_OFF,
                                       ANDROID_CONTROL_AF_MODE_AUTO});
    update_request(ANDROID_CONTROL_AF_MODE, ANDROID_CONTROL_AF_MODE_AUTO);
  } else {
    update_static(ANDROID_CONTROL_AF_AVAILABLE_MODES,
                  ANDROID_CONTROL_AF_MODE_OFF);
    update_request(ANDROID_CONTROL_AF_MODE, ANDROID_CONTROL_AF_MODE_OFF);
    // If auto focus is not supported, the minimum focus distance should be 0.
    // Overwrite the value here since there are many camera modules have wrong
    // config.
    update_static(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE, 0.0f);
  }

  // Set vendor tags for specified boards.
  if (device_info.quirks & kQuirkMonocle) {
    int32_t timestamp_sync =
        static_cast<int32_t>(mojom::CameraSensorSyncTimestamp::NEAREST);
    update_static(kVendorTagTimestampSync, timestamp_sync);
  }

  return update_static.ok() && update_request.ok() ? 0 : -EINVAL;
}

const camera_metadata_t* MetadataHandler::GetDefaultRequestSettings(
    int template_type) {
  if (!IsValidTemplateType(template_type)) {
    LOGF(ERROR) << "Invalid template request type: " << template_type;
    return nullptr;
  }
  return template_settings_[template_type].get();
}

int MetadataHandler::PreHandleRequest(int frame_number,
                                      const Size& resolution,
                                      android::CameraMetadata* metadata) {
  DCHECK(thread_checker_.CalledOnValidThread());
  MetadataUpdater update_request(metadata);

  if (metadata->exists(ANDROID_CONTROL_AF_TRIGGER)) {
    camera_metadata_entry entry = metadata->find(ANDROID_CONTROL_AF_TRIGGER);
    if (entry.data.u8[0] == ANDROID_CONTROL_AF_TRIGGER_START) {
      af_trigger_ = true;
    } else if (entry.data.u8[0] == ANDROID_CONTROL_AF_TRIGGER_CANCEL) {
      af_trigger_ = false;
    }
  }

  if (metadata->exists(ANDROID_CONTROL_AF_MODE)) {
    camera_metadata_entry entry = metadata->find(ANDROID_CONTROL_AF_MODE);
    if (entry.data.u8[0] == ANDROID_CONTROL_AF_MODE_OFF) {
      device_->SetAutoFocus(false);
    } else if (entry.data.u8[0] == ANDROID_CONTROL_AF_MODE_AUTO) {
      device_->SetAutoFocus(true);
    }
  }

  const int64_t rolling_shutter_skew =
      sensor_handler_->GetRollingShutterSkew(resolution);
  update_request(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW, rolling_shutter_skew);

  const int64_t exposure_time = sensor_handler_->GetExposureTime(resolution);
  update_request(ANDROID_SENSOR_EXPOSURE_TIME, exposure_time);

  current_frame_number_ = frame_number;
  return 0;
}

int MetadataHandler::PostHandleRequest(int frame_number,
                                       int64_t timestamp,
                                       android::CameraMetadata* metadata) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (current_frame_number_ != frame_number) {
    LOGF(ERROR)
        << "Frame number mismatch in PreHandleRequest and PostHandleRequest";
    return -EINVAL;
  }

  MetadataUpdater update_request(metadata);
  // android.control
  // For USB camera, we don't know the AE state. Set the state to converged to
  // indicate the frame should be good to use. Then apps don't have to wait the
  // AE state.
  update_request(ANDROID_CONTROL_AE_STATE, ANDROID_CONTROL_AE_STATE_CONVERGED);
  update_request(ANDROID_CONTROL_AE_LOCK, ANDROID_CONTROL_AE_LOCK_OFF);

  // For USB camera, the USB camera handles everything and we don't have control
  // over AF. We only simply fake the AF metadata based on the request
  // received here.
  uint8_t af_state;
  if (af_trigger_) {
    af_state = ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
  } else {
    af_state = ANDROID_CONTROL_AF_STATE_INACTIVE;
  }
  update_request(ANDROID_CONTROL_AF_STATE, af_state);

  // Set AWB state to converged to indicate the frame should be good to use.
  update_request(ANDROID_CONTROL_AWB_STATE,
                 ANDROID_CONTROL_AWB_STATE_CONVERGED);

  update_request(ANDROID_CONTROL_AWB_LOCK, ANDROID_CONTROL_AWB_LOCK_OFF);

  camera_metadata_entry active_array_size =
      static_metadata_.find(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);

  if (active_array_size.count == 0) {
    LOGF(ERROR) << "Active array size is not found.";
    return -EINVAL;
  }

  // android.lens
  // Since android.lens.focalLength, android.lens.focusDistance and
  // android.lens.aperture are all fixed. And we don't support
  // android.lens.filterDensity so we can set the state to stationary.
  update_request(ANDROID_LENS_STATE, ANDROID_LENS_STATE_STATIONARY);

  // android.scaler
  update_request(ANDROID_SCALER_CROP_REGION, std::vector<int32_t>{
                                                 active_array_size.data.i32[0],
                                                 active_array_size.data.i32[1],
                                                 active_array_size.data.i32[2],
                                                 active_array_size.data.i32[3],
                                             });

  // android.sensor
  update_request(ANDROID_SENSOR_TIMESTAMP, timestamp);

  // android.statistics
  update_request(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE,
                 ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF);

  update_request(ANDROID_STATISTICS_SCENE_FLICKER,
                 ANDROID_STATISTICS_SCENE_FLICKER_NONE);
  return 0;
}

bool MetadataHandler::IsValidTemplateType(int template_type) {
  return template_type > 0 && template_type < CAMERA3_TEMPLATE_COUNT;
}

ScopedCameraMetadata MetadataHandler::CreateDefaultRequestSettings(
    int template_type) {
  android::CameraMetadata data(request_template_);

  int ret;
  switch (template_type) {
    case CAMERA3_TEMPLATE_PREVIEW:
      ret = FillDefaultPreviewSettings(&data);
      break;
    case CAMERA3_TEMPLATE_STILL_CAPTURE:
      ret = FillDefaultStillCaptureSettings(&data);
      break;
    case CAMERA3_TEMPLATE_VIDEO_RECORD:
      ret = FillDefaultVideoRecordSettings(&data);
      break;
    case CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:
      ret = FillDefaultVideoSnapshotSettings(&data);
      break;
    case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
      ret = FillDefaultZeroShutterLagSettings(&data);
      break;
    case CAMERA3_TEMPLATE_MANUAL:
      ret = FillDefaultManualSettings(&data);
      break;
    default:
      LOGF(ERROR) << "Invalid template request type: " << template_type;
      return NULL;
  }

  if (ret) {
    return ScopedCameraMetadata();
  }
  return ScopedCameraMetadata(data.release());
}

int MetadataHandler::FillDefaultPreviewSettings(
    android::CameraMetadata* metadata) {
  MetadataUpdater update_request(metadata);

  // android.control
  update_request(ANDROID_CONTROL_CAPTURE_INTENT,
                 ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW);
  update_request(ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
                 std::vector<int32_t>{kMinFps, max_supported_fps_});

  update_request(ANDROID_CONTROL_MODE, ANDROID_CONTROL_MODE_AUTO);
  return 0;
}

int MetadataHandler::FillDefaultStillCaptureSettings(
    android::CameraMetadata* metadata) {
  MetadataUpdater update_request(metadata);

  // android.colorCorrection
  update_request(ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
                 ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY);

  // android.control
  update_request(ANDROID_CONTROL_CAPTURE_INTENT,
                 ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE);
  update_request(ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
                 std::vector<int32_t>{kMinFps, max_supported_fps_});

  update_request(ANDROID_CONTROL_MODE, ANDROID_CONTROL_MODE_AUTO);
  return 0;
}

int MetadataHandler::FillDefaultVideoRecordSettings(
    android::CameraMetadata* metadata) {
  MetadataUpdater update_request(metadata);

  // android.control
  update_request(ANDROID_CONTROL_CAPTURE_INTENT,
                 ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD);
  update_request(ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
                 std::vector<int32_t>{max_supported_fps_, max_supported_fps_});

  update_request(ANDROID_CONTROL_MODE, ANDROID_CONTROL_MODE_AUTO);
  return 0;
}

int MetadataHandler::FillDefaultVideoSnapshotSettings(
    android::CameraMetadata* metadata) {
  MetadataUpdater update_request(metadata);

  // android.control
  update_request(ANDROID_CONTROL_CAPTURE_INTENT,
                 ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT);
  update_request(ANDROID_CONTROL_AE_TARGET_FPS_RANGE,
                 std::vector<int32_t>{max_supported_fps_, max_supported_fps_});

  update_request(ANDROID_CONTROL_MODE, ANDROID_CONTROL_MODE_AUTO);
  return 0;
}

int MetadataHandler::FillDefaultZeroShutterLagSettings(
    android::CameraMetadata* /*metadata*/) {
  // Do not support ZSL template.
  return -EINVAL;
}

int MetadataHandler::FillDefaultManualSettings(
    android::CameraMetadata* /*metadata*/) {
  // Do not support manual template.
  return -EINVAL;
}

}  // namespace cros
