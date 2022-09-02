/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal/fake/metadata_handler.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <base/containers/fixed_flat_set.h>
#include <camera/camera_metadata.h>
#include <hardware/camera3.h>

#include "cros-camera/common.h"

namespace cros {

// TODO(pihsun): Move this into common/ to remove duplication with USB HAL.
class MetadataUpdater {
 public:
  explicit MetadataUpdater(android::CameraMetadata* metadata)
      : metadata_(metadata) {}

  bool ok() { return ok_; }
  std::vector<camera_metadata_tag> updated_tags() { return updated_tags_; }

  template <typename T>
  void operator()(camera_metadata_tag tag, const std::vector<T>& data) {
    if (!ok_) {
      return;
    }
    if (metadata_->update(tag, data) != 0) {
      ok_ = false;
      LOGF(ERROR) << "Update metadata with tag " << std::hex << std::showbase
                  << tag << " failed" << std::dec;
    } else {
      updated_tags_.push_back(tag);
    }
  }

  template <typename T>
  std::enable_if_t<std::is_enum<T>::value> operator()(
      camera_metadata_tag tag, std::initializer_list<T> data) {
    static constexpr auto kInt32EnumTags =
        base::MakeFixedFlatSet<camera_metadata_tag>({
            ANDROID_DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS,
            ANDROID_SCALER_AVAILABLE_FORMATS,
            ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,
            ANDROID_SENSOR_TEST_PATTERN_MODE,
            ANDROID_SYNC_MAX_LATENCY,
        });
    if (kInt32EnumTags.contains(tag)) {
      std::vector<int32_t> v;
      v.reserve(data.size());
      std::transform(data.begin(), data.end(), std::back_inserter(v),
                     [](T e) { return static_cast<int32_t>(e); });
      operator()(tag, v);
    } else {
      std::vector<uint8_t> v;
      v.reserve(data.size());
      std::transform(data.begin(), data.end(), std::back_inserter(v),
                     [](T e) { return base::checked_cast<uint8_t>(e); });
      operator()(tag, v);
    }
  }

  template <typename T>
  std::enable_if_t<!std::is_enum<T>::value> operator()(
      camera_metadata_tag tag, std::initializer_list<T> data) {
    operator()(tag, std::vector<T>(data));
  }

  template <typename T>
  void operator()(camera_metadata_tag tag, const T& data) {
    operator()(tag, {data});
  }

 private:
  android::CameraMetadata* metadata_;
  bool ok_ = true;
  std::vector<camera_metadata_tag> updated_tags_;
};

absl::Status FillDefaultMetadata(android::CameraMetadata* static_metadata,
                                 android::CameraMetadata* request_metadata) {
  MetadataUpdater update_static(static_metadata);
  MetadataUpdater update_request(request_metadata);

  // TODO(pihsun): All these values should be derived from the supported
  // formats in camera config.
  constexpr int32_t kWidth = 1920;
  constexpr int32_t kHeight = 1080;
  constexpr int32_t kThumbnailWidth = 192;
  constexpr int32_t kThumbnailHeight = 108;
  constexpr int32_t kFps = 60;
  constexpr int64_t kOneSecOfNanoUnit = 1000000000LL;
  constexpr int64_t kFrameDuration =
      kOneSecOfNanoUnit / static_cast<double>(kFps);

  // android.colorCorrection
  update_static(ANDROID_COLOR_CORRECTION_AVAILABLE_ABERRATION_MODES,
                ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST);
  update_request(ANDROID_COLOR_CORRECTION_ABERRATION_MODE,
                 ANDROID_COLOR_CORRECTION_ABERRATION_MODE_FAST);

  // android.control
  update_static(ANDROID_CONTROL_AE_AVAILABLE_ANTIBANDING_MODES,
                ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO);
  update_request(ANDROID_CONTROL_AE_ANTIBANDING_MODE,
                 ANDROID_CONTROL_AE_ANTIBANDING_MODE_AUTO);

  update_static(ANDROID_CONTROL_AE_AVAILABLE_MODES, ANDROID_CONTROL_AE_MODE_ON);
  update_request(ANDROID_CONTROL_AE_MODE, ANDROID_CONTROL_AE_MODE_ON);

  // TODO(pihsun): This should be derived from supported formats.
  update_static(ANDROID_CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES, {kFps, kFps});
  update_request(ANDROID_CONTROL_AE_TARGET_FPS_RANGE, {kFps, kFps});

  // We don't support AE compensation.
  update_static(ANDROID_CONTROL_AE_COMPENSATION_RANGE, {0, 0});

  update_static(ANDROID_CONTROL_AE_COMPENSATION_STEP,
                camera_metadata_rational_t{0, 1});

  update_request(ANDROID_CONTROL_AE_EXPOSURE_COMPENSATION, 0);

  update_request(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER,
                 ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_IDLE);

  update_static(ANDROID_CONTROL_AE_LOCK_AVAILABLE,
                ANDROID_CONTROL_AE_LOCK_AVAILABLE_FALSE);
  update_request(ANDROID_CONTROL_AE_LOCK, ANDROID_CONTROL_AE_LOCK_OFF);

  update_static(ANDROID_CONTROL_AF_AVAILABLE_MODES,
                ANDROID_CONTROL_AF_MODE_OFF);
  update_request(ANDROID_CONTROL_AF_MODE, ANDROID_CONTROL_AF_MODE_OFF);

  update_request(ANDROID_CONTROL_AF_TRIGGER, ANDROID_CONTROL_AF_TRIGGER_IDLE);

  update_static(ANDROID_CONTROL_AVAILABLE_EFFECTS,
                ANDROID_CONTROL_EFFECT_MODE_OFF);
  update_request(ANDROID_CONTROL_EFFECT_MODE, ANDROID_CONTROL_EFFECT_MODE_OFF);

  update_static(ANDROID_CONTROL_AVAILABLE_MODES,
                {ANDROID_CONTROL_MODE_OFF, ANDROID_CONTROL_MODE_AUTO});
  update_request(ANDROID_CONTROL_MODE, ANDROID_CONTROL_MODE_AUTO);

  update_static(ANDROID_CONTROL_AVAILABLE_SCENE_MODES,
                ANDROID_CONTROL_SCENE_MODE_DISABLED);
  update_request(ANDROID_CONTROL_SCENE_MODE,
                 ANDROID_CONTROL_SCENE_MODE_DISABLED);

  update_static(ANDROID_CONTROL_AVAILABLE_VIDEO_STABILIZATION_MODES,
                ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF);
  update_request(ANDROID_CONTROL_VIDEO_STABILIZATION_MODE,
                 ANDROID_CONTROL_VIDEO_STABILIZATION_MODE_OFF);

  update_static(ANDROID_CONTROL_AWB_AVAILABLE_MODES,
                ANDROID_CONTROL_AWB_MODE_AUTO);
  update_request(ANDROID_CONTROL_AWB_MODE, ANDROID_CONTROL_AWB_MODE_AUTO);

  update_static(ANDROID_CONTROL_AWB_LOCK_AVAILABLE,
                ANDROID_CONTROL_AWB_LOCK_AVAILABLE_FALSE);
  update_request(ANDROID_CONTROL_AWB_LOCK, ANDROID_CONTROL_AWB_LOCK_OFF);

  // TODO(pihsun): This should be set on construct_default_request_settings
  // based on request type.
  update_request(ANDROID_CONTROL_CAPTURE_INTENT,
                 ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW);

  update_static(ANDROID_CONTROL_MAX_REGIONS, {/*AE*/ 0, /*AWB*/ 0, /*AF*/ 0});

  update_request(ANDROID_CONTROL_ZOOM_RATIO, 1.0f);

  // android.flash
  update_static(ANDROID_FLASH_INFO_AVAILABLE,
                ANDROID_FLASH_INFO_AVAILABLE_FALSE);
  update_request(ANDROID_FLASH_MODE, ANDROID_FLASH_MODE_OFF);

  // android.info
  update_static(ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL,
                ANDROID_INFO_SUPPORTED_HARDWARE_LEVEL_EXTERNAL);

  // android.jpeg
  // TODO(pihsun): This should be derived from supported formats.
  update_static(ANDROID_JPEG_AVAILABLE_THUMBNAIL_SIZES,
                {0, 0, kThumbnailWidth, kThumbnailHeight});

  // TODO(pihsun): Check if this is large enough.
  update_static(ANDROID_JPEG_MAX_SIZE, 13 << 20);

  update_request(ANDROID_JPEG_QUALITY, uint8_t{90});
  update_request(ANDROID_JPEG_THUMBNAIL_QUALITY, uint8_t{90});

  // TODO(pihsun): This should be derived from supported formats.
  update_request(ANDROID_JPEG_THUMBNAIL_SIZE,
                 {kThumbnailWidth, kThumbnailHeight});

  update_request(ANDROID_JPEG_ORIENTATION, 0);

  // android.lens
  update_static(ANDROID_LENS_INFO_AVAILABLE_OPTICAL_STABILIZATION,
                ANDROID_LENS_OPTICAL_STABILIZATION_MODE_OFF);
  update_static(ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION,
                ANDROID_LENS_INFO_FOCUS_DISTANCE_CALIBRATION_UNCALIBRATED);

  update_static(ANDROID_LENS_INFO_HYPERFOCAL_DISTANCE, 0.0f);

  update_static(ANDROID_LENS_INFO_MINIMUM_FOCUS_DISTANCE, 0.0f);

  update_static(ANDROID_LENS_FACING, ANDROID_LENS_FACING_EXTERNAL);

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

  // Limited mode doesn't support reprocessing.
  update_static(ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS, 0);

  // Three numbers represent the maximum numbers of different types of output
  // streams simultaneously. The types are raw sensor, processed (but not
  // stalling), and processed (but stalling).
  update_static(ANDROID_REQUEST_MAX_NUM_OUTPUT_STREAMS, {0, 2, 1});

  // This means pipeline latency of X frame intervals.
  // TODO(pihsun): Check the actual value we need for
  // android.request.pipelineDepth, this would also affect the number of
  // prepared buffers somewhere in the stack.
  update_static(ANDROID_REQUEST_PIPELINE_MAX_DEPTH, uint8_t{2});

  // android.scaler
  update_static(ANDROID_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM, 1.0f);

  // TODO(pihsun): This should be derived from supported formats.
  update_static(
      ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
      std::vector<int64_t>{HAL_PIXEL_FORMAT_BLOB, kWidth, kHeight,
                           kFrameDuration, HAL_PIXEL_FORMAT_YCbCr_420_888,
                           kWidth, kHeight, kFrameDuration});

  update_static(ANDROID_SCALER_AVAILABLE_ROTATE_AND_CROP_MODES,
                ANDROID_SCALER_ROTATE_AND_CROP_NONE);
  update_request(ANDROID_SCALER_ROTATE_AND_CROP,
                 ANDROID_SCALER_ROTATE_AND_CROP_NONE);

  // TODO(pihsun): This should be derived from supported formats.
  update_static(
      ANDROID_SCALER_AVAILABLE_STALL_DURATIONS,
      std::vector<int64_t>{HAL_PIXEL_FORMAT_BLOB, kWidth, kHeight, 0,
                           HAL_PIXEL_FORMAT_YCbCr_420_888, kWidth, kHeight, 0});

  // TODO(pihsun): This currently doesn't satisfy the requirement, since 240p,
  // 480p, 720p is missing.
  update_static(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                std::vector<int32_t>{
                    HAL_PIXEL_FORMAT_BLOB, kWidth, kHeight,
                    ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
                    HAL_PIXEL_FORMAT_YCbCr_420_888, kWidth, kHeight,
                    ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT});

  update_static(ANDROID_SCALER_CROPPING_TYPE,
                ANDROID_SCALER_CROPPING_TYPE_CENTER_ONLY);

  // android.sensor
  std::vector<int32_t> active_array_size = {0, 0, kWidth, kHeight};

  update_static(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE, active_array_size);
  update_static(ANDROID_SENSOR_INFO_PIXEL_ARRAY_SIZE, {kWidth, kHeight});
  update_static(ANDROID_SENSOR_INFO_PRE_CORRECTION_ACTIVE_ARRAY_SIZE,
                active_array_size);

  update_static(ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE,
                ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME);

  // TODO(pihsun): Support test patterns
  update_static(ANDROID_SENSOR_AVAILABLE_TEST_PATTERN_MODES,
                ANDROID_SENSOR_TEST_PATTERN_MODE_OFF);
  update_request(ANDROID_SENSOR_TEST_PATTERN_MODE,
                 ANDROID_SENSOR_TEST_PATTERN_MODE_OFF);

  update_static(ANDROID_SENSOR_ORIENTATION, 0);

  // android.shading
  update_static(ANDROID_SHADING_AVAILABLE_MODES, ANDROID_SHADING_MODE_FAST);
  update_request(ANDROID_SHADING_MODE, ANDROID_SHADING_MODE_FAST);

  // android.statistics
  update_static(ANDROID_STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES,
                ANDROID_STATISTICS_FACE_DETECT_MODE_OFF);
  update_request(ANDROID_STATISTICS_FACE_DETECT_MODE,
                 ANDROID_STATISTICS_FACE_DETECT_MODE_OFF);

  update_static(ANDROID_STATISTICS_INFO_AVAILABLE_HOT_PIXEL_MAP_MODES,
                ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF);
  update_request(ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE,
                 ANDROID_STATISTICS_HOT_PIXEL_MAP_MODE_OFF);

  update_static(ANDROID_STATISTICS_INFO_AVAILABLE_LENS_SHADING_MAP_MODES,
                ANDROID_STATISTICS_LENS_SHADING_MAP_MODE_OFF);

  update_static(ANDROID_STATISTICS_INFO_AVAILABLE_OIS_DATA_MODES,
                ANDROID_STATISTICS_OIS_DATA_MODE_OFF);
  update_request(ANDROID_STATISTICS_OIS_DATA_MODE,
                 ANDROID_STATISTICS_OIS_DATA_MODE_OFF);

  update_static(ANDROID_STATISTICS_INFO_MAX_FACE_COUNT, 0);

  // android.sync
  update_static(ANDROID_SYNC_MAX_LATENCY, ANDROID_SYNC_MAX_LATENCY_UNKNOWN);

  // android.request.available*
  std::vector<camera_metadata_tag> static_tags = update_static.updated_tags();
  std::vector<camera_metadata_tag> request_tags = update_request.updated_tags();

  update_static(ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS,
                std::vector<int32_t>(static_tags.begin(), static_tags.end()));

  // TODO(pihsun): Not all tags will be listed here when we construct metadata
  // from spec. Fill the rest of tags when needed.
  update_static(ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
                std::vector<int32_t>(request_tags.begin(), request_tags.end()));

  std::vector<camera_metadata_tag> result_tags = request_tags;
  result_tags.insert(result_tags.end(),
                     {ANDROID_CONTROL_AE_STATE, ANDROID_CONTROL_AF_STATE,
                      ANDROID_CONTROL_AWB_STATE, ANDROID_FLASH_STATE,
                      ANDROID_LENS_STATE, ANDROID_REQUEST_PIPELINE_DEPTH});

  update_static(ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
                std::vector<int32_t>(result_tags.begin(), result_tags.end()));

  return update_static.ok() && update_request.ok()
             ? absl::OkStatus()
             : absl::InternalError("metadata update");
}

absl::Status FillResultMetadata(android::CameraMetadata* metadata) {
  MetadataUpdater update(metadata);

  update(ANDROID_CONTROL_AE_STATE, ANDROID_CONTROL_AE_STATE_CONVERGED);
  update(ANDROID_CONTROL_AF_STATE, ANDROID_CONTROL_AF_STATE_INACTIVE);
  update(ANDROID_CONTROL_AWB_STATE, ANDROID_CONTROL_AWB_STATE_CONVERGED);
  update(ANDROID_FLASH_STATE, ANDROID_FLASH_STATE_UNAVAILABLE);
  update(ANDROID_LENS_STATE, ANDROID_LENS_STATE_STATIONARY);
  update(ANDROID_REQUEST_PIPELINE_DEPTH, uint8_t{2});

  return update.ok() ? absl::OkStatus()
                     : absl::InternalError("metadata update");
}

}  // namespace cros
