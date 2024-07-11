/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "camera/features/kiosk_vision/kiosk_vision_stream_manipulator.h"

#include <cstdint>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/notreached.h>
#include <base/time/time.h>
#include <ml_core/dlc/dlc_ids.h>

#include "camera/features/kiosk_vision/tracing.h"
#include "camera/mojo/cros_camera_service.mojom.h"
#include "cros-camera/camera_metadata_utils.h"
#include "features/kiosk_vision/kiosk_vision_wrapper.h"

namespace {

// Path for a json file to override pipeline options.
constexpr char kOverrideKioskVisionConfigFile[] =
    "/run/camera/kiosk_vision_config.json";

// Json key to switch debug visualization on/off.
constexpr char kKeyDebug[] = "debug";

// Json key to set a processing frame rate limit.
constexpr char kKeyFrameTimeout[] = "frame_timeout_ms";

// Minimum acceptable timeout between frame processing.
// Appr. corresponds to a max frame rate of 30 FPS.
constexpr int64_t kMinFrameTimeoutMs = 33;

// Maximum acceptable timeout between frame processing.
// Corresponds to a min frame rate of 1 FPS.
constexpr int64_t kMaxFrameTimeoutMs = 1000;

// Checks that the stream manipulator options are valid.
void CheckOptions(const cros::KioskVisionStreamManipulator::Options& options) {
  // Frame timeout (and thus framerate) is within reasonable range.
  CHECK_GE(options.frame_timeout_ms, kMinFrameTimeoutMs);
  CHECK_LE(options.frame_timeout_ms, kMaxFrameTimeoutMs);
}

int64_t GetCurrentTimestampUs() {
  return base::Time::Now().ToDeltaSinceWindowsEpoch().InMicroseconds();
}

bool IsLargerOrCloserToNativeAspectRatio(
    const camera3_stream_t* lhs,
    const camera3_stream_t* rhs,
    const cros::Size& active_array_dimension) {
  if (lhs->width >= rhs->width && lhs->height >= rhs->height) {
    return true;
  }
  if (lhs->width <= rhs->width && lhs->height <= rhs->height) {
    return false;
  }
  float active_aspect_ratio = static_cast<float>(active_array_dimension.width) /
                              static_cast<float>(active_array_dimension.height);
  float lhs_aspect_ratio =
      static_cast<float>(lhs->width) / static_cast<float>(lhs->height);
  float rhs_aspect_ratio =
      static_cast<float>(rhs->width) / static_cast<float>(rhs->height);
  return std::abs(lhs_aspect_ratio - active_aspect_ratio) <=
         std::abs(rhs_aspect_ratio - active_aspect_ratio);
}

cros::mojom::KioskVisionBoundingBoxPtr BboxToMojom(
    const cros::kiosk_vision::Rect& input) {
  return cros::mojom::KioskVisionBoundingBox::New(
      /*x=*/input.x_min,
      /*y=*/input.y_min,
      /*width=*/input.x_max - input.x_min,
      /*height=*/input.y_max - input.y_min);
}

cros::mojom::KioskVisionBodyDetectionPtr BodyToMojom(
    const cros::kiosk_vision::BodyDetection& input) {
  return cros::mojom::KioskVisionBodyDetection::New(
      /*confidence=*/input.confidence,
      /*box=*/BboxToMojom(input.bounding_box));
}

cros::mojom::KioskVisionFaceDetectionPtr FaceToMojom(
    const cros::kiosk_vision::FaceDetection& input) {
  return cros::mojom::KioskVisionFaceDetection::New(
      /*confidence=*/input.confidence,
      /*roll=*/input.angles.roll,
      /*pan=*/input.angles.pan,
      /*tilt=*/input.angles.tilt,
      /*box=*/BboxToMojom(input.bounding_box));
}

cros::mojom::KioskVisionAppearancePtr AppearanceToMojom(
    const cros::kiosk_vision::Appearance& input) {
  return cros::mojom::KioskVisionAppearance::New(
      /*timestamp_in_us=*/input.timestamp,
      /*person_id=*/input.track_id,
      /*face=*/input.is_face_detected ? FaceToMojom(input.face) : nullptr,
      /*body=*/input.is_body_detected ? BodyToMojom(input.body) : nullptr);
}

std::vector<cros::mojom::KioskVisionAppearancePtr> AppearancesToMojom(
    const cros::kiosk_vision::Appearance* data, uint32_t size) {
  std::vector<cros::mojom::KioskVisionAppearancePtr> result;
  result.reserve(size);
  for (uint32_t i = 0; i < size; ++i) {
    const cros::kiosk_vision::Appearance& current = data[i];

    // Only send appearances with real detections this frame. Skip empty
    // 'placeholder' appearances from existing tracks that were not associated
    // in this frame.
    if (current.is_face_detected || current.is_body_detected) {
      result.emplace_back(AppearanceToMojom(current));
    }
  }
  return result;
}

cros::KioskVisionStreamManipulator::Status ConvertStatus(
    cros::KioskVisionWrapper::InitializeStatus wrapper_status) {
  switch (wrapper_status) {
    case cros::KioskVisionWrapper::InitializeStatus::kOk:
      return cros::KioskVisionStreamManipulator::Status::kInitialized;
    case cros::KioskVisionWrapper::InitializeStatus::kDlcError:
      return cros::KioskVisionStreamManipulator::Status::kDlcError;
    case cros::KioskVisionWrapper::InitializeStatus::kPipelineError:
    case cros::KioskVisionWrapper::InitializeStatus::kInputBufferError:
      return cros::KioskVisionStreamManipulator::Status::kModelError;
  }
}

cros::mojom::KioskVisionError ConvertErrorStatusToMojom(
    cros::KioskVisionStreamManipulator::Status error_status) {
  switch (error_status) {
    case cros::KioskVisionStreamManipulator::Status::kNotInitialized:
    case cros::KioskVisionStreamManipulator::Status::kInitialized:
      NOTREACHED_NORETURN()
          << "Cannot convert non-error status to mojom error.";
    case cros::KioskVisionStreamManipulator::Status::kUnknownError:
      return cros::mojom::KioskVisionError::UNKNOWN;
    case cros::KioskVisionStreamManipulator::Status::kDlcError:
      return cros::mojom::KioskVisionError::DLC_ERROR;
    case cros::KioskVisionStreamManipulator::Status::kModelError:
      return cros::mojom::KioskVisionError::MODEL_ERROR;
  }
}

}  // namespace

namespace cros {

KioskVisionStreamManipulator::KioskVisionStreamManipulator(
    RuntimeOptions* runtime_options,
    const scoped_refptr<base::SingleThreadTaskRunner>& ipc_thread_task_runner)
    : KioskVisionStreamManipulator(
          runtime_options,
          ipc_thread_task_runner,
          std::make_unique<KioskVisionWrapper>(
              base::BindRepeating(
                  &KioskVisionStreamManipulator::OnFrameProcessed,
                  base::Unretained(this)),
              base::BindRepeating(
                  &KioskVisionStreamManipulator::OnTrackCompleted,
                  base::Unretained(this)),
              base::BindRepeating(&KioskVisionStreamManipulator::OnError,
                                  base::Unretained(this)))) {}

KioskVisionStreamManipulator::KioskVisionStreamManipulator(
    RuntimeOptions* runtime_options,
    const scoped_refptr<base::SingleThreadTaskRunner>& ipc_thread_task_runner,
    std::unique_ptr<KioskVisionWrapper> kiosk_vision_wrapper)
    : config_(ReloadableConfigFile::Options{
          .override_config_file_path =
              base::FilePath(kOverrideKioskVisionConfigFile)}),
      dlc_path_(runtime_options->GetDlcRootPath(dlc_client::kKioskVisionDlcId)),
      observer_(runtime_options->GetKioskVisionObserver()),
      ipc_thread_task_runner_(ipc_thread_task_runner),
      kiosk_vision_wrapper_(std::move(kiosk_vision_wrapper)) {
  config_.SetCallback(base::BindRepeating(
      &KioskVisionStreamManipulator::OnOptionsUpdated, base::Unretained(this)));

  CheckOptions(options_);
  LOGF(INFO) << "KioskVisionStreamManipulator is created";
}

KioskVisionStreamManipulator::~KioskVisionStreamManipulator() = default;

bool KioskVisionStreamManipulator::Initialize(
    const camera_metadata_t* static_info, Callbacks callbacks) {
  TRACE_KIOSK_VISION();
  callbacks_ = std::move(callbacks);

  base::span<const int32_t> active_array_size = GetRoMetadataAsSpan<int32_t>(
      static_info, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);
  DCHECK_EQ(active_array_size.size(), 4);
  VLOGF(1) << "active_array_size: (" << active_array_size[0] << ", "
           << active_array_size[1] << "), (" << active_array_size[2] << ", "
           << active_array_size[3] << ")";
  active_array_dimension_ = Size(active_array_size[2], active_array_size[3]);
  if (!active_array_dimension_.is_valid()) {
    LOGF(ERROR) << "Invalid active array dimension: "
                << active_array_dimension_.ToString();
    UpdateStatus(Status::kUnknownError);
    return false;
  }

  VLOGF(1) << "KioskVisionStreamManipulator init. DLC path: " << dlc_path_;
  KioskVisionWrapper::InitializeStatus initialize_status =
      kiosk_vision_wrapper_->Initialize(dlc_path_);

  UpdateStatus(ConvertStatus(initialize_status));

  // Detector size is ok to cast if init above succeeds.
  auto input_size = kiosk_vision_wrapper_->GetDetectorInputSize();
  detector_input_size_ = {static_cast<uint32_t>(input_size.width),
                          static_cast<uint32_t>(input_size.height)};

  return (status_ == Status::kInitialized);
}

bool KioskVisionStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  TRACE_KIOSK_VISION();
  return true;
}

bool KioskVisionStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  TRACE_KIOSK_VISION();
  return true;
}

bool KioskVisionStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  TRACE_KIOSK_VISION();
  return true;
}

bool KioskVisionStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  TRACE_KIOSK_VISION("frame_number", request->frame_number());
  return true;
}

bool KioskVisionStreamManipulator::Flush() {
  return true;
}

bool KioskVisionStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor result) {
  TRACE_KIOSK_VISION("frame_number", result.frame_number());

  if (status_ != Status::kInitialized) {
    return false;
  }

  base::ScopedClosureRunner result_callback_task =
      StreamManipulator::MakeScopedCaptureResultCallbackRunner(
          callbacks_.result_callback, result);

  // Uses latest detections for debugging visualization.
  if (options_.enable_debug_visualization) {
    SetDebugMetadata(&result);
  }

  // TODO(sbykov): Use sensor timestamp (ANDROID_SENSOR_TIMESTAMP)
  const int64_t current_timestamp_us = GetCurrentTimestampUs();

  // Caps effective framerate of the pipeline. Frames will be skipped for a
  // specified timeout since previous processed frame.
  // TODO(b/339399663): Don't process new frames if Mojo remote is unbound.
  {
    base::AutoLock lock(lock_);
    const int64_t kFrameTimeoutUs = options_.frame_timeout_ms * 1000;
    if (current_timestamp_us - processed_frame_timestamp_us_ <
        kFrameTimeoutUs) {
      return true;
    }
    processed_frame_timestamp_us_ = current_timestamp_us;
  }

  Camera3StreamBuffer* selected_buffer = SelectInputBuffer(result);
  if (!selected_buffer) {
    LOGF(ERROR) << "No buffer selected for timestamp " << current_timestamp_us;
    return false;
  }

  buffer_handle_t input_buffer = *(selected_buffer->buffer());
  kiosk_vision_wrapper_->ProcessFrame(current_timestamp_us, input_buffer);
  return true;
}

void KioskVisionStreamManipulator::Notify(camera3_notify_msg_t msg) {
  callbacks_.notify_callback.Run(std::move(msg));
}

const base::FilePath& KioskVisionStreamManipulator::GetDlcPathForTesting()
    const {
  return dlc_path_;
}

KioskVisionStreamManipulator::Status
KioskVisionStreamManipulator::GetStatusForTesting() const {
  return status_;
}

Camera3StreamBuffer* KioskVisionStreamManipulator::SelectInputBuffer(
    Camera3CaptureDescriptor& result) {
  Camera3StreamBuffer* result_buffer = nullptr;
  auto output_buffers = result.GetMutableOutputBuffers();

  for (auto& current_buffer : output_buffers) {
    const auto* current_stream = current_buffer.stream();
    if (current_stream->stream_type != CAMERA3_STREAM_OUTPUT) {
      continue;
    }

    // TODO(sbykov): Is 10-bit YUV support needed (i.e. with format
    // HAL_PIXEL_FORMAT_YCBCR_P010);
    if (current_stream->format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
        current_stream->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
      if (current_stream->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED &&
          (current_stream->usage & GRALLOC_USAGE_HW_CAMERA_ZSL) ==
              GRALLOC_USAGE_HW_CAMERA_ZSL) {
        // Ignore ZSL streams.
        continue;
      }

      // Pick a buffer for controller. This is a heuristic and shouldn't
      // matter for the majority of the time, as for most cases the requested
      // streams would have the same aspect ratio.
      if (!result_buffer || IsLargerOrCloserToNativeAspectRatio(
                                current_stream, result_buffer->stream(),
                                active_array_dimension_)) {
        result_buffer = &current_buffer;
      }
    }
  }

  if (!result_buffer) {
    LOGF(WARNING) << "No YUV stream suitable for CrOS Kiosk Vision";
    return nullptr;
  }

  constexpr int kSyncWaitTimeoutMs = 300;
  if (!result_buffer->WaitOnAndClearReleaseFence(kSyncWaitTimeoutMs)) {
    LOGF(ERROR) << "Timed out waiting for detection buffer";
    return nullptr;
  }

  return result_buffer;
}

void KioskVisionStreamManipulator::SetDebugMetadata(
    Camera3CaptureDescriptor* result) {
  if (!result->has_metadata()) {
    LOGF(ERROR) << "Cannot set data. Camera3CaptureDescriptor has no metadata";
    return;
  }

  if (!result->UpdateMetadata<uint8_t>(
          ANDROID_STATISTICS_FACE_DETECT_MODE,
          (uint8_t[]){ANDROID_STATISTICS_FACE_DETECT_MODE_SIMPLE})) {
    LOGF(ERROR) << "Cannot set ANDROID_STATISTICS_FACE_DETECT_MODE";
    return;
  }

  // Use android metadata to draw rectangles in the camera app.
  std::vector<int32_t> output_coordinates;
  std::vector<uint8_t> output_scores;

  {
    base::AutoLock lock(lock_);
    output_coordinates.reserve(latest_audience_result_.size() * 8);
    for (const auto& appearance : latest_audience_result_) {
      if (appearance.is_body_detected) {
        auto& bbox = appearance.body.bounding_box;
        float conf = appearance.body.confidence;

        output_coordinates.push_back(DebugScaleWidth(bbox.x_min));
        output_coordinates.push_back(DebugScaleHeight(bbox.y_min));
        output_coordinates.push_back(DebugScaleWidth(bbox.x_max));
        output_coordinates.push_back(DebugScaleHeight(bbox.y_max));

        output_scores.push_back(
            static_cast<uint8_t>(std::clamp(conf * 100.0f, 0.0f, 100.0f)));
      }
      if (appearance.is_face_detected) {
        auto& bbox = appearance.face.bounding_box;
        float conf = appearance.face.confidence;

        output_coordinates.push_back(DebugScaleWidth(bbox.x_min));
        output_coordinates.push_back(DebugScaleHeight(bbox.y_min));
        output_coordinates.push_back(DebugScaleWidth(bbox.x_max));
        output_coordinates.push_back(DebugScaleHeight(bbox.y_max));

        output_scores.push_back(
            static_cast<uint8_t>(std::clamp(conf * 100.0f, 0.0f, 100.0f)));
      }
    }
  }

  if (!result->UpdateMetadata<int32_t>(ANDROID_STATISTICS_FACE_RECTANGLES,
                                       output_coordinates)) {
    LOGF(ERROR) << "Cannot set ANDROID_STATISTICS_FACE_RECTANGLES";
  }
  if (!result->UpdateMetadata<uint8_t>(ANDROID_STATISTICS_FACE_SCORES,
                                       output_scores)) {
    LOGF(ERROR) << "Cannot set ANDROID_STATISTICS_FACE_SCORES";
  }
}

int32_t KioskVisionStreamManipulator::DebugScaleWidth(int32_t original_width) {
  return static_cast<int32_t>((1.0f * original_width) *
                              active_array_dimension_.width /
                              detector_input_size_.width);
}

int32_t KioskVisionStreamManipulator::DebugScaleHeight(
    int32_t original_height) {
  return static_cast<int32_t>((1.0f * original_height) *
                              active_array_dimension_.height /
                              detector_input_size_.height);
}

void KioskVisionStreamManipulator::OnOptionsUpdated(
    const base::Value::Dict& json_values) {
  LoadIfExist(json_values, kKeyDebug, &options_.enable_debug_visualization);

  int frame_timeout_ms = 0;
  if (LoadIfExist(json_values, kKeyFrameTimeout, &frame_timeout_ms)) {
    options_.frame_timeout_ms = base::checked_cast<int64_t>(frame_timeout_ms);
  }

  VLOGF(1) << "Kiosk Vision config updated: [frame_timeout_ms: "
           << options_.frame_timeout_ms << "; enable_debug_visualization: "
           << options_.enable_debug_visualization << "]";

  CheckOptions(options_);
}

void KioskVisionStreamManipulator::OnFrameProcessed(
    cros::kiosk_vision::Timestamp timestamp,
    const cros::kiosk_vision::Appearance* audience_data,
    uint32_t audience_size) {
  // Save results for debug visualization.
  if (options_.enable_debug_visualization) {
    base::AutoLock lock(lock_);
    latest_audience_result_.assign(audience_data,
                                   audience_data + audience_size);
  }

  // Forward results to browser.
  mojom::KioskVisionDetectionPtr result = mojom::KioskVisionDetection::New(
      timestamp, AppearancesToMojom(audience_data, audience_size));

  ipc_thread_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](raw_ref<mojo::Remote<mojom::KioskVisionObserver>> observer,
             mojom::KioskVisionDetectionPtr result) {
            if (observer->is_bound()) {
              (*observer)->OnFrameProcessed(std::move(result));
            } else {
              LOGF(ERROR) << "OnFrameProcessed ipc error. Unbound remote";
            }
          },
          observer_, std::move(result)));
}

void KioskVisionStreamManipulator::OnTrackCompleted(
    cros::kiosk_vision::TrackID id,
    const cros::kiosk_vision::Appearance* appearances_data,
    uint32_t appearances_size,
    cros::kiosk_vision::Timestamp start_time,
    cros::kiosk_vision::Timestamp end_time) {
  mojom::KioskVisionTrackPtr result = mojom::KioskVisionTrack::New(
      /*person_id=*/id,
      /*start_timestamp_in_us=*/start_time,
      /*end_timestamp_in_us=*/end_time,
      /*appearances=*/AppearancesToMojom(appearances_data, appearances_size));

  ipc_thread_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](raw_ref<mojo::Remote<mojom::KioskVisionObserver>> observer,
             mojom::KioskVisionTrackPtr result) {
            if (observer->is_bound()) {
              (*observer)->OnTrackCompleted(std::move(result));
            } else {
              LOGF(ERROR) << "OnTrackCompleted ipc error. Unbound remote";
            }
          },
          observer_, std::move(result)));
}

void KioskVisionStreamManipulator::OnError() {
  UpdateStatus(Status::kModelError);
}

void KioskVisionStreamManipulator::UpdateStatus(Status status) {
  status_ = status;

  switch (status) {
    case Status::kInitialized:
    case Status::kNotInitialized:
      break;
    case Status::kUnknownError:
    case Status::kDlcError:
    case Status::kModelError:
      ReportError(status);
  }
}

void KioskVisionStreamManipulator::ReportError(Status error_status) {
  LOGF(ERROR) << "Report error to the observer: "
              << static_cast<int>(error_status);

  // TODO(b/339399663): Error handling. Recreate the pipeline.
  ipc_thread_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](raw_ref<mojo::Remote<mojom::KioskVisionObserver>> observer,
             Status error_status) {
            if (observer->is_bound()) {
              (*observer)->OnError(ConvertErrorStatusToMojom(error_status));
            } else {
              LOGF(ERROR) << "OnError ipc error. Unbound remote";
            }
          },
          observer_, error_status));
}

}  // namespace cros
