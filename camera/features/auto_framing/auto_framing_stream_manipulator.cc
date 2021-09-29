/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/auto_framing/auto_framing_stream_manipulator.h"

#include "cros-camera/camera_metadata_utils.h"

namespace cros {

namespace {

constexpr char kMetadataDumpPath[] =
    "/run/camera/auto_framing_frame_metadata.json";

constexpr char kEnableKey[] = "enable";
constexpr char kDebugKey[] = "debug";

}  // namespace

//
// AutoFramingStreamManipulator implementations.
//

AutoFramingStreamManipulator::AutoFramingStreamManipulator()
    : config_(base::FilePath(kDefaultAutoFramingConfigFile),
              base::FilePath(kOverrideAutoFramingConfigFile)),
      gpu_thread_("AutoFramingGpuThread"),
      metadata_logger_({.dump_path = base::FilePath(kMetadataDumpPath)}) {
  config_.SetCallback(base::BindRepeating(
      &AutoFramingStreamManipulator::OnOptionsUpdated, base::Unretained(this)));
  CHECK(gpu_thread_.Start());
}

AutoFramingStreamManipulator::~AutoFramingStreamManipulator() {
  gpu_thread_.Stop();
}

bool AutoFramingStreamManipulator::Initialize(
    const camera_metadata_t* static_info,
    CaptureResultCallback result_callback) {
  base::span<const int32_t> active_array_size = GetRoMetadataAsSpan<int32_t>(
      static_info, ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE);
  DCHECK_EQ(active_array_size.size(), 4);
  VLOGF(2) << "active_array_size: (" << active_array_size[0] << ", "
           << active_array_size[1] << "), (" << active_array_size[2] << ", "
           << active_array_size[3] << ")";
  active_array_dimension_ = Size(active_array_size[2], active_array_size[3]);
  return true;
}

bool AutoFramingStreamManipulator::ConfigureStreams(
    Camera3StreamConfiguration* stream_config) {
  Reset();

  base::AutoLock lock(lock_);

  // TODO(jcliang): Modify the stream configuration to use the largest possible
  // 30fps YUV stream.

  for (auto* s : stream_config->GetStreams()) {
    if (s->stream_type != CAMERA3_STREAM_OUTPUT) {
      continue;
    }
    if (s->format == HAL_PIXEL_FORMAT_YCbCr_420_888 ||
        s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
      if (s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED &&
          (s->usage & GRALLOC_USAGE_HW_CAMERA_ZSL) ==
              GRALLOC_USAGE_HW_CAMERA_ZSL) {
        // Ignore ZSL streams.
        continue;
      }

      // Pick the buffer with the largest width to AE controller. This is a
      // heuristic and shouldn't matter for the majority of the time, as for
      // most cases the requested streams would have the same aspect ratio.
      if (!yuv_stream_ || s->width > yuv_stream_->width) {
        yuv_stream_ = s;
      }
    }
  }

  if (yuv_stream_) {
    VLOGF(1) << "YUV stream for CrOS Auto Framing processing: "
             << GetDebugString(yuv_stream_);
    face_tracker_ = std::make_unique<FaceTracker>(FaceTracker::Options{
        .active_array_dimension = active_array_dimension_});
    frame_cropper_ = std::make_unique<FrameCropper>(
        FrameCropper::Options{
            .input_size = {yuv_stream_->width, yuv_stream_->height}},
        gpu_thread_.task_runner());
  } else {
    LOGF(WARNING) << "No YUV stream suitable for CrOS Auto Framing processing";
  }

  return true;
}

bool AutoFramingStreamManipulator::OnConfiguredStreams(
    Camera3StreamConfiguration* stream_config) {
  return true;
}

bool AutoFramingStreamManipulator::ConstructDefaultRequestSettings(
    android::CameraMetadata* default_request_settings, int type) {
  // TODO(jcliang): Fill in the PTZ vendor tags.
  return true;
}

bool AutoFramingStreamManipulator::ProcessCaptureRequest(
    Camera3CaptureDescriptor* request) {
  if (!options_.enable) {
    return true;
  }
  if (request->GetInputBuffer() != nullptr) {
    // Skip reprocessing requests.
    return true;
  }

  // TODO(jcliang): Replace buffer.
  return true;
}

bool AutoFramingStreamManipulator::ProcessCaptureResult(
    Camera3CaptureDescriptor* result) {
  if (!options_.enable) {
    return true;
  }

  base::AutoLock lock(lock_);
  if (!frame_cropper_ || !face_tracker_) {
    return false;
  }

  if (result->feature_metadata().faces) {
    face_tracker_->OnNewFaceData(*result->feature_metadata().faces);
    faces_ = face_tracker_->GetActiveFaceRectangles();
    region_of_interest_ = face_tracker_->GetActiveBoundingRectangle();
    gpu_thread_.PostTaskSync(
        FROM_HERE,
        base::BindOnce(&FrameCropper::OnNewRegionOfInterest,
                       base::Unretained(frame_cropper_.get()),
                       result->frame_number(), std::cref(region_of_interest_)));
  }

  CropBuffer(result);
  UpdateFaceRectangleMetadata(result);

  return true;
}

bool AutoFramingStreamManipulator::Notify(camera3_notify_msg_t* msg) {
  return true;
}

bool AutoFramingStreamManipulator::Flush() {
  return true;
}

void AutoFramingStreamManipulator::OnOptionsUpdated(
    const base::Value& json_values) {
  LoadIfExist(json_values, kEnableKey, &options_.enable);
  LoadIfExist(json_values, kDebugKey, &options_.debug);
  VLOGF(1) << "AutoFramingStreamManipulator options:"
           << " enable=" << options_.enable << " debug=" << options_.debug;
  if (face_tracker_) {
    face_tracker_->OnOptionsUpdated(json_values);
  }
  if (frame_cropper_) {
    gpu_thread_.PostTaskSync(
        FROM_HERE, base::BindOnce(&FrameCropper::OnOptionsUpdated,
                                  base::Unretained(frame_cropper_.get()),
                                  std::cref(json_values)));
  }
}

void AutoFramingStreamManipulator::Reset() {
  base::AutoLock lock(lock_);
  yuv_stream_ = nullptr;
  face_tracker_ = nullptr;
  frame_cropper_ = nullptr;
}

void AutoFramingStreamManipulator::CropBuffer(
    Camera3CaptureDescriptor* result) {
  lock_.AssertAcquired();

  if (result->num_output_buffers() == 0) {
    return;
  }
  if (options_.debug) {
    // In debug mode we draw the crop area on the full frame instead.
    return;
  }
  for (auto& b : result->GetOutputBuffers()) {
    if (b.stream == yuv_stream_) {
      base::ScopedFD release_fence;
      gpu_thread_.PostTaskSync(
          FROM_HERE,
          base::BindOnce(&FrameCropper::CropBuffer,
                         base::Unretained(frame_cropper_.get()),
                         result->frame_number(), *b.buffer,
                         base::ScopedFD(b.release_fence), *b.buffer),
          &release_fence);
      const_cast<camera3_stream_buffer_t&>(b).release_fence =
          release_fence.release();
    }
  }
}

void cros::AutoFramingStreamManipulator::UpdateFaceRectangleMetadata(
    Camera3CaptureDescriptor* result) {
  if (!result->has_metadata()) {
    return;
  }

  std::vector<Rect<float>> face_rectangles;
  if (options_.debug) {
    // Show the detected faces, aggregated region of interest and the active
    // crop region in debug mode.
    face_rectangles = faces_;
    face_rectangles.push_back(region_of_interest_);
    Rect<float> active_crop_region;
    gpu_thread_.PostTaskSync(
        FROM_HERE,
        base::BindOnce(&FrameCropper::GetActiveCropRegion,
                       base::Unretained(frame_cropper_.get())),
        &active_crop_region);
    face_rectangles.push_back(active_crop_region);
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
    gpu_thread_.PostTaskSync(
        FROM_HERE, base::BindOnce(&FrameCropper::ConvertToCropSpace,
                                  base::Unretained(frame_cropper_.get()),
                                  base::Unretained(&face_rectangles)));
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

}  // namespace cros
