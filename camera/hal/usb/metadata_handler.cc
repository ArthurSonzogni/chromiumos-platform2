/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/usb/metadata_handler.h"

#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

#include "common/utils/camera_config.h"
#include "cros-camera/common.h"
#include "hal/usb/stream_format.h"
#include "hal/usb/v4l2_camera_device.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define UPDATE(tag, data, size)                      \
  do {                                               \
    if (metadata->update((tag), (data), (size))) {   \
      LOGF(ERROR) << "Update " << #tag << " failed"; \
      return -EINVAL;                                \
    }                                                \
  } while (0)

namespace cros {

MetadataHandler::MetadataHandler(const camera_metadata_t& metadata)
    : af_trigger_(false) {
  // MetadataBase::operator= will make a copy of camera_metadata_t.
  metadata_ = &metadata;

  // camera3_request_template_t starts at 1.
  for (int i = 1; i < CAMERA3_TEMPLATE_COUNT; i++) {
    template_settings_[i] = CreateDefaultRequestSettings(i);
  }

  thread_checker_.DetachFromThread();
}

MetadataHandler::~MetadataHandler() {}

int MetadataHandler::FillDefaultMetadata(android::CameraMetadata* metadata) {
  // android.colorCorrection
  const uint8_t available_aberration_modes[] = {
      ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST,
      ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY};
  UPDATE(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
         available_aberration_modes, ARRAY_SIZE(available_aberration_modes));
  UPDATE(ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
         &available_aberration_modes[0], 1);

  // android.control
  // We don't support AE compensation.
  const int32_t control_ae_compensation_range[] = {0, 0};
  UPDATE(ANDROID_CONTROL_AE_COMPENSATION_RANGE, control_ae_compensation_range,
         ARRAY_SIZE(control_ae_compensation_range));

  const camera_metadata_rational_t control_ae_compensation_step[] = {{0, 1}};
  UPDATE(ANDROID_CONTROL_AE_COMPENSATION_STEP, control_ae_compensation_step,
         ARRAY_SIZE(control_ae_compensation_step));

  const int32_t control_max_regions[] = {/*AE*/ 0, /*AWB*/ 0, /*AF*/ 0};
  UPDATE(ANDROID_CONTROL_MAX_REGIONS, control_max_regions,
         ARRAY_SIZE(control_max_regions));

  const uint8_t video_stabilization_mode =
      ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF;
  UPDATE(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
         &video_stabilization_mode, 1);
  UPDATE(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE, &video_stabilization_mode,
         1);

  const uint8_t awb_available_mode = ANDROID_CONTROL_AWB_MODE_AUTO;
  UPDATE(ANDROID_CONTROL_AWB_AVAILABLE_MODES, &awb_available_mode, 1);
  UPDATE(ANDROID_CONTROL_AWB_MODE, &awb_available_mode, 1);

  const uint8_t ae_available_mode = ANDROID_CONTROL_AE_MODE_ON;
  UPDATE(ANDROID_CONTROL_AE_AVAILABLE_MODES, &ae_available_mode, 1);
  // ON means auto-exposure is active with no flash control.
  UPDATE(ANDROID_CONTROL_AE_MODE, &ae_available_mode, 1);

  const int32_t ae_exposure_compensation = 0;
  UPDATE(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, &ae_exposure_compensation,
         1);

  const uint8_t ae_precapture_trigger =
      ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE;
  UPDATE(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER, &ae_precapture_trigger, 1);

  const uint8_t af_available_modes[] = {ANDROID_CONTROL_AF_MODE_AUTO,
                                        ANDROID_CONTROL_AF_MODE_OFF};
  UPDATE(ANDROID_CONTROL_AF_AVAILABLE_MODES, af_available_modes,
         ARRAY_SIZE(af_available_modes));
  UPDATE(ANDROID_CONTROL_AF_MODE, &af_available_modes[0], 1);

  const uint8_t af_trigger = ANDROID_CONTROL_AF_TRIGGER_IDLE;
  UPDATE(ANDROID_CONTROL_AF_TRIGGER, &af_trigger, 1);

  const uint8_t available_scene_mode = ANDROID_CONTROL_SCENE_MODE_DISABLED;
  UPDATE(ANDROID_CONTROL_AVAILABLE_SCENE_MODES, &available_scene_mode, 1);
  UPDATE(ANDROID_CONTROL_SCENE_MODE, &available_scene_mode, 1);

  const uint8_t available_effect = ANDROID_CONTROL_EFFECT_MODE_OFF;
  UPDATE(ANDROID_CONTROL_AVAILABLE_EFFECTS, &available_effect, 1);
  UPDATE(ANDROID_CONTROL_EFFECT_MODE, &available_effect, 1);

  const uint8_t ae_lock_available = ANDROID_CONTROL_AE_LOCK_AVAILABLE_FALSE;
  UPDATE(ANDROID_CONTROL_AE_LOCK_AVAILABLE, &ae_lock_available, 1);

  const uint8_t awb_lock_available = ANDROID_CONTROL_AWB_LOCK_AVAILABLE_FALSE;
  UPDATE(ANDROID_CONTROL_AWB_LOCK_AVAILABLE, &awb_lock_available, 1);

  const uint8_t control_available_modes[] = {ANDROID_CONTROL_MODE_OFF,
                                             ANDROID_CONTROL_MODE_AUTO};
  UPDATE(ANDROID_CONTROL_AVAILABLE_MODES, control_available_modes,
         ARRAY_SIZE(control_available_modes));

  // android.flash
  const uint8_t flash_info = ANDROID_FLASH_INFO_AVAILABLE_FALSE;
  UPDATE(ANDROID_FLASH_INFO_AVAILABLE, &flash_info, 1);

  const uint8_t flash_state = ANDROID_FLASH_STATE_UNAVAILABLE;
  UPDATE(ANDROID_FLASH_STATE, &flash_state, 1);

  const uint8_t flash_mode = ANDROID_FLASH_MODE_OFF;
  UPDATE(ANDROID_FLASH_MODE, &flash_mode, 1);

  // android.jpeg
  const int32_t jpeg_max_size[] = {13 * 1024 * 1024};  // 13MB
  UPDATE(ANDROID_JPEG_MAX_SIZE, jpeg_max_size, ARRAY_SIZE(jpeg_max_size));

  const uint8_t jpeg_quality = 90;
  UPDATE(ANDROID_JPEG_QUALITY, &jpeg_quality, 1);
  UPDATE(ANDROID_JPEG_THUMBNAIL_QUALITY, &jpeg_quality, 1);

  const int32_t jpeg_orientation = 0;
  UPDATE(ANDROID_JPEG_ORIENTATION, &jpeg_orientation, 1);

  // android.lens
  // This should not be needed.
  const float hyper_focal_distance = 0.0f;
  UPDATE(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE, &hyper_focal_distance, 1);

  const uint8_t optical_stabilization_mode =
      ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF;
  UPDATE(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
         &optical_stabilization_mode, 1);
  UPDATE(ANDROID_LENS_OPTICAL_STABILIZATION_MODE, &optical_stabilization_mode,
         1);

  // android.noiseReduction
  const uint8_t noise_reduction_mode = ANDROID_NOISE_REDUCTION_MODE_OFF;
  UPDATE(ANDROID_NOISE_REDUCTION_AVAILABLE_NOISE_REDUCTION_MODES,
         &noise_reduction_mode, 1);
  UPDATE(ANDROID_NOISE_REDUCTION_MODE, &noise_reduction_mode, 1);

  // android.request
  const uint8_t available_capabilities[] = {
      ANDROID_REQUEST_AVAILABLE_CAPABILITIES_BACKWARD_COMPATIBLE};
  UPDATE(ANDROID_REQUEST_AVAILABLE_CAPABILITIES, available_capabilities,
         ARRAY_SIZE(available_capabilities));

  const int32_t partial_result_count = 1;
  UPDATE(ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &partial_result_count, 1);

  // This means pipeline latency of X frame intervals. The maximum number is 4.
  const uint8_t request_pipeline_max_depth = 4;
  UPDATE(ANDROID_REQUEST_PIPELINE_MAX_DEPTH, &request_pipeline_max_depth, 1);
  UPDATE(ANDROID_REQUEST_PIPELINE_DEPTH, &request_pipeline_max_depth, 1);

  // Three numbers represent the maximum numbers of different types of output
  // streams simultaneously. The types are raw sensor, processed (but not
  // stalling), and processed (but stalling). For usb limited mode, raw sensor
  // is not supported. Stalling stream is JPEG. Non-stalling streams are
  // YUV_420_888, NV21, or YV12.
  const int32_t request_max_num_output_streams[] = {0, 2, 1};
  UPDATE(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, request_max_num_output_streams,
         ARRAY_SIZE(request_max_num_output_streams));

  // Limited mode doesn't support reprocessing.
  const int32_t request_max_num_input_streams = 0;
  UPDATE(ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS, &request_max_num_input_streams,
         1);

  // android.scaler
  const float scaler_available_max_digital_zoom[] = {1};
  UPDATE(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM,
         scaler_available_max_digital_zoom,
         ARRAY_SIZE(scaler_available_max_digital_zoom));

  const uint8_t cropping_type = ANDROID_SCALER_CROPPING_TYPE_CENTER_ONLY;
  UPDATE(ANDROID_SCALER_CROPPING_TYPE, &cropping_type, 1);

  const int32_t test_pattern_modes[] = {
      ANDROID_SENSOR_TEST_PATTERN_MODE_OFF,
      ANDROID_SENSOR_TEST_PATTERN_MODE_COLOR_BARS_FADE_TO_GRAY};
  UPDATE(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES, test_pattern_modes,
         ARRAY_SIZE(test_pattern_modes));
  UPDATE(ANDROID_SENSOR_TEST_PATTERN_MODE, &test_pattern_modes[0], 1);

  uint8_t timestamp_source;
  if (V4L2CameraDevice::GetUvcClock() == CLOCK_BOOTTIME) {
    timestamp_source = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME;
  } else {
    timestamp_source = ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN;
  }
  UPDATE(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, &timestamp_source, 1);

  // android.shading
  const uint8_t availabe_mode = ANDROID_SHADING_MODE_FAST;
  UPDATE(ANDROID_SHADING_AVAILABLE_MODES, &availabe_mode, 1);

  // android.statistics
  const uint8_t face_detect_mode = ANDROID_STATISTICS_FACE_DETECT_MODE_OFF;
  UPDATE(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES, &face_detect_mode,
         1);
  UPDATE(ANDROID_STATISTICS_FACE_DETECT_MODE, &face_detect_mode, 1);

  const int32_t max_face_count = 0;
  UPDATE(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, &max_face_count, 1);

  const uint8_t available_hotpixel_mode =
      ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF;
  UPDATE(ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
         &available_hotpixel_mode, 1);
  UPDATE(ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE, &available_hotpixel_mode, 1);

  const uint8_t lens_shading_map_mode =
      ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
  UPDATE(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
         &lens_shading_map_mode, 1);

  // android.sync
  const int32_t max_latency = ANDROID_SYNC_MAX_LATENCY_UNKNOWN;
  UPDATE(ANDROID_SYNC_MAX_LATENCY, &max_latency, 1);
  return 0;
}

int MetadataHandler::FillMetadataFromSupportedFormats(
    const SupportedFormats& supported_formats,
    android::CameraMetadata* metadata,
    bool is_external) {
  if (supported_formats.empty()) {
    return -EINVAL;
  }
  std::vector<int32_t> stream_configurations;
  std::vector<int64_t> min_frame_durations;
  std::vector<int64_t> stall_durations;

  // The min fps <= 15 must be supported in CTS.
  const int32_t kMinFpsMax = 1;
  const int64_t kOneSecOfNanoUnit = 1000000000LL;
  int32_t max_fps = std::numeric_limits<int32_t>::min();
  int32_t min_fps = kMinFpsMax;
  int64_t max_frame_duration = kOneSecOfNanoUnit / min_fps;

  std::vector<int> hal_formats{HAL_PIXEL_FORMAT_BLOB,
                               HAL_PIXEL_FORMAT_YCbCr_420_888,
                               HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED};

  std::unordered_map<int, int> max_hal_width_by_format;
  std::unordered_map<int, int> max_hal_height_by_format;
  CameraConfig camera_config(constants::kCrosCameraConfigPathString);
  max_hal_width_by_format[HAL_PIXEL_FORMAT_BLOB] = camera_config.GetInteger(
      constants::kCrosMaxBlobWidth, std::numeric_limits<int>::max());
  max_hal_width_by_format[HAL_PIXEL_FORMAT_YCbCr_420_888] =
      camera_config.GetInteger(constants::kCrosMaxYuvWidth,
                               std::numeric_limits<int>::max());
  max_hal_width_by_format[HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED] =
      camera_config.GetInteger(constants::kCrosMaxPrivateWidth,
                               std::numeric_limits<int>::max());

  max_hal_height_by_format[HAL_PIXEL_FORMAT_BLOB] = camera_config.GetInteger(
      constants::kCrosMaxBlobHeight, std::numeric_limits<int>::max());
  max_hal_height_by_format[HAL_PIXEL_FORMAT_YCbCr_420_888] =
      camera_config.GetInteger(constants::kCrosMaxYuvHeight,
                               std::numeric_limits<int>::max());
  max_hal_height_by_format[HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED] =
      camera_config.GetInteger(constants::kCrosMaxPrivateHeight,
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
    }
    if (per_format_max_fps > max_fps) {
      max_fps = per_format_max_fps;
    }

    for (const auto& format : hal_formats) {
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
      // We filter the resolution which cannot support 30 fps out.
      if (format == HAL_PIXEL_FORMAT_BLOB || per_format_max_fps == 30 ||
          is_external) {
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

  // The document in aeAvailableTargetFpsRanges section says the min_fps should
  // not be larger than 15.
  // We cannot support fixed 30fps but Android requires (min, max) and
  // (max, max) ranges.
  int32_t fps_ranges[] = {min_fps, max_fps, max_fps, max_fps};
  UPDATE(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, fps_ranges,
         ARRAY_SIZE(fps_ranges));

  // CTS expects the (maxFps == minFps) for recording.
  int32_t ae_fps_ranges[] = {max_fps, max_fps};
  UPDATE(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, ae_fps_ranges, 2);

  UPDATE(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
         stream_configurations.data(), stream_configurations.size());

  UPDATE(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
         min_frame_durations.data(), min_frame_durations.size());

  UPDATE(ANDROID_SCALER_AVAILABLE_STALL_DURATIONS, stall_durations.data(),
         stall_durations.size());

  std::vector<int32_t> jpeg_available_thumbnail_sizes =
      GetJpegAvailableThumbnailSizes(supported_formats);
  UPDATE(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
         jpeg_available_thumbnail_sizes.data(),
         jpeg_available_thumbnail_sizes.size());
  UPDATE(ANDROID_JPEG_THUMBNAIL_SIZE,
         &jpeg_available_thumbnail_sizes.back() - 1, 2);

  UPDATE(ANDROID_SENSOR_INFO_MAX_FRAME_DURATION, &max_frame_duration, 1);

  SupportedFormat maximum_format = GetMaximumFormat(supported_formats);
  int32_t active_array_size[] = {0, 0,
                                 static_cast<int32_t>(maximum_format.width),
                                 static_cast<int32_t>(maximum_format.height)};
  UPDATE(ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
         active_array_size, ARRAY_SIZE(active_array_size));
  UPDATE(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, active_array_size,
         ARRAY_SIZE(active_array_size));

  if (is_external) {
    // It's a sensible value for external camera, since it's required on all
    // devices per spec.  For built-in camera, this would be filled in
    // FillMetadataFromDeviceInfo() using the value from the configuration file.
    // References:
    // * The official document for this field
    //   https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics.html#SENSOR_INFO_PIXEL_ARRAY_SIZE
    // * The implementation of external camera in Android P
    //   https://googleplex-android.git.corp.google.com/platform/hardware/interfaces/+/6ad8708bf8b631561fa11eb1f4889907d1772d78/camera/device/3.4/default/ExternalCameraDevice.cpp#687
    int32_t pixel_array_size[] = {static_cast<int32_t>(maximum_format.width),
                                  static_cast<int32_t>(maximum_format.height)};
    UPDATE(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, pixel_array_size,
           ARRAY_SIZE(pixel_array_size));
  }

  return 0;
}

int MetadataHandler::FillMetadataFromDeviceInfo(
    const DeviceInfo& device_info, android::CameraMetadata* metadata) {
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
  UPDATE(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS, available_request_keys.data(),
         available_request_keys.size());

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
  UPDATE(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS, available_result_keys.data(),
         available_result_keys.size());

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
  UPDATE(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
         available_characteristics_keys.data(),
         available_characteristics_keys.size());

  UPDATE(ANDROID_SENSOR_ORIENTATION, &device_info.sensor_orientation, 1);

  uint8_t lens_facing = device_info.lens_facing;
  UPDATE(ANDROID_LENS_FACING, &lens_facing, 1);

  if (is_builtin) {
    uint8_t hardware_level = ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_LIMITED;
    UPDATE(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, &hardware_level, 1);

    UPDATE(ANDROID_LENS_INFO_AVAILABLE_APERTURES,
           device_info.lens_info_available_apertures.data(),
           device_info.lens_info_available_apertures.size());

    UPDATE(ANDROID_LENS_APERTURE,
           &device_info.lens_info_available_apertures.data()[0], 1);

    UPDATE(ANDROID_LENS_INFO_AVAILABLE_FOCAL_LENGTHS,
           device_info.lens_info_available_focal_lengths.data(),
           device_info.lens_info_available_focal_lengths.size());

    UPDATE(ANDROID_LENS_FOCAL_LENGTH,
           &device_info.lens_info_available_focal_lengths.data()[0], 1);

    UPDATE(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE,
           &device_info.lens_info_minimum_focus_distance, 1);

    UPDATE(ANDROID_LENS_FOCUS_DISTANCE,
           &device_info.lens_info_optimal_focus_distance, 1);

    float physical_size[] = {device_info.sensor_info_physical_size_width,
                             device_info.sensor_info_physical_size_height};
    UPDATE(ANDROID_SENSOR_INFO_PHYSICAL_SIZE, physical_size,
           ARRAY_SIZE(physical_size));

    int32_t pixel_array_size[] = {
        device_info.sensor_info_pixel_array_size_width,
        device_info.sensor_info_pixel_array_size_height};
    UPDATE(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, pixel_array_size,
           ARRAY_SIZE(pixel_array_size));
  } else {
    uint8_t hardware_level = ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_EXTERNAL;
    UPDATE(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL, &hardware_level, 1);
  }

  uint8_t focus_distance_calibration =
      ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_UNCALIBRATED;
  UPDATE(ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
         &focus_distance_calibration, 1);

  uint8_t ae_antibanding_mode = ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO;
  UPDATE(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES, &ae_antibanding_mode,
         1);
  UPDATE(ANDROID_CONTROL_AE_ANTIBANDING_MODE, &ae_antibanding_mode, 1);

  return 0;
}

const camera_metadata_t* MetadataHandler::GetDefaultRequestSettings(
    int template_type) {
  if (!IsValidTemplateType(template_type)) {
    LOGF(ERROR) << "Invalid template request type: " << template_type;
    return nullptr;
  }
  return template_settings_[template_type].get();
}

void MetadataHandler::PreHandleRequest(
    int frame_number, const android::CameraMetadata& metadata) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (metadata.exists(ANDROID_CONTROL_AF_TRIGGER)) {
    camera_metadata_ro_entry entry = metadata.find(ANDROID_CONTROL_AF_TRIGGER);
    if (entry.data.u8[0] == ANDROID_CONTROL_AF_TRIGGER_START) {
      af_trigger_ = true;
    } else if (entry.data.u8[0] == ANDROID_CONTROL_AF_TRIGGER_CANCEL) {
      af_trigger_ = false;
    }
  }
  current_frame_number_ = frame_number;
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

  // android.control
  // For USB camera, we don't know the AE state. Set the state to converged to
  // indicate the frame should be good to use. Then apps don't have to wait the
  // AE state.
  const uint8_t ae_state = ANDROID_CONTROL_AE_STATE_CONVERGED;
  UPDATE(ANDROID_CONTROL_AE_STATE, &ae_state, 1);

  const uint8_t ae_lock = ANDROID_CONTROL_AE_LOCK_OFF;
  UPDATE(ANDROID_CONTROL_AE_LOCK, &ae_lock, 1);

  // For USB camera, the USB camera handles everything and we don't have control
  // over AF. We only simply fake the AF metadata based on the request
  // received here.
  uint8_t af_state;
  if (af_trigger_) {
    af_state = ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED;
  } else {
    af_state = ANDROID_CONTROL_AF_STATE_INACTIVE;
  }
  UPDATE(ANDROID_CONTROL_AF_STATE, &af_state, 1);

  // Set AWB state to converged to indicate the frame should be good to use.
  const uint8_t awb_state = ANDROID_CONTROL_AWB_STATE_CONVERGED;
  UPDATE(ANDROID_CONTROL_AWB_STATE, &awb_state, 1);

  const uint8_t awb_lock = ANDROID_CONTROL_AWB_LOCK_OFF;
  UPDATE(ANDROID_CONTROL_AWB_LOCK, &awb_lock, 1);

  camera_metadata_entry active_array_size =
      metadata->find(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);

  if (active_array_size.count == 0) {
    LOGF(ERROR) << "Active array size is not found.";
    return -EINVAL;
  }

  // android.lens
  // Since android.lens.focalLength, android.lens.focusDistance and
  // android.lens.aperture are all fixed. And we don't support
  // android.lens.filterDensity so we can set the state to stationary.
  const uint8_t lens_state = ANDROID_LENS_STATE_STATIONARY;
  UPDATE(ANDROID_LENS_STATE, &lens_state, 1);

  // android.scaler
  const int32_t crop_region[] = {
      active_array_size.data.i32[0],
      active_array_size.data.i32[1],
      active_array_size.data.i32[2],
      active_array_size.data.i32[3],
  };
  UPDATE(ANDROID_SCALER_CROP_REGION, crop_region, ARRAY_SIZE(crop_region));

  // android.sensor
  UPDATE(ANDROID_SENSOR_TIMESTAMP, &timestamp, 1);

  // Rolling shutter skew and exposure time values are fake due to ARCore
  // test requirement.
  // TODO(henryhsu): Read the two values from camera.
  const int64_t rolling_shutter_skew = 33'300'000;  // 33.3ms
  UPDATE(ANDROID_SENSOR_ROLLING_SHUTTER_SKEW, &rolling_shutter_skew, 1);

  const int64_t exposure_time = 16'600'000;  // 16.6ms
  UPDATE(ANDROID_SENSOR_EXPOSURE_TIME, &exposure_time, 1);

  // android.statistics
  const uint8_t lens_shading_map_mode =
      ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF;
  UPDATE(ANDROID_STATISTICS_LENS_SHADING_MAP_MODE, &lens_shading_map_mode, 1);

  const uint8_t scene_flicker = ANDROID_STATISTICS_SCENE_FLICKER_NONE;
  UPDATE(ANDROID_STATISTICS_SCENE_FLICKER, &scene_flicker, 1);
  return 0;
}

bool MetadataHandler::IsValidTemplateType(int template_type) {
  return template_type > 0 && template_type < CAMERA3_TEMPLATE_COUNT;
}

CameraMetadataUniquePtr MetadataHandler::CreateDefaultRequestSettings(
    int template_type) {
  android::CameraMetadata data(metadata_);

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
    return CameraMetadataUniquePtr();
  }
  return CameraMetadataUniquePtr(data.release());
}

int MetadataHandler::FillDefaultPreviewSettings(
    android::CameraMetadata* metadata) {
  // android.control
  const uint8_t capture_intent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
  UPDATE(ANDROID_CONTROL_CAPTURE_INTENT, &capture_intent, 1);

  const uint8_t control_mode = ANDROID_CONTROL_MODE_AUTO;
  UPDATE(ANDROID_CONTROL_MODE, &control_mode, 1);
  return 0;
}

int MetadataHandler::FillDefaultStillCaptureSettings(
    android::CameraMetadata* metadata) {
  // android.colorCorrection
  const uint8_t color_aberration_mode =
      ANDROID_COLOR_CORRECTION_ABERRATION_MODE_HIGH_QUALITY;
  UPDATE(ANDROID_COLOR_CORRECTION_ABERRATION_MODE, &color_aberration_mode, 1);

  // android.control
  const uint8_t capture_intent = ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE;
  UPDATE(ANDROID_CONTROL_CAPTURE_INTENT, &capture_intent, 1);

  const uint8_t control_mode = ANDROID_CONTROL_MODE_AUTO;
  UPDATE(ANDROID_CONTROL_MODE, &control_mode, 1);
  return 0;
}

int MetadataHandler::FillDefaultVideoRecordSettings(
    android::CameraMetadata* metadata) {
  // android.control
  const uint8_t capture_intent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD;
  UPDATE(ANDROID_CONTROL_CAPTURE_INTENT, &capture_intent, 1);

  const uint8_t control_mode = ANDROID_CONTROL_MODE_AUTO;
  UPDATE(ANDROID_CONTROL_MODE, &control_mode, 1);
  return 0;
}

int MetadataHandler::FillDefaultVideoSnapshotSettings(
    android::CameraMetadata* metadata) {
  // android.control
  const uint8_t capture_intent = ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT;
  UPDATE(ANDROID_CONTROL_CAPTURE_INTENT, &capture_intent, 1);

  const uint8_t control_mode = ANDROID_CONTROL_MODE_AUTO;
  UPDATE(ANDROID_CONTROL_MODE, &control_mode, 1);
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
