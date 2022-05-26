/*
 * Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/auto_framing/tests/auto_framing_test_fixture.h"

#include <utility>

#include "features/auto_framing/auto_framing_stream_manipulator.h"

namespace cros::tests {

namespace {

bool IsAspectRatioMatched(const Rect<float>& crop,
                          uint32_t src_width,
                          uint32_t src_height,
                          uint32_t dst_width,
                          uint32_t dst_height) {
  return std::abs((static_cast<float>(src_width) * crop.width) /
                      (static_cast<float>(src_height) * crop.height) -
                  static_cast<float>(dst_width) /
                      static_cast<float>(dst_height)) < 1e-2f;
}

bool IsRoiOnFace(const Rect<float>& roi, const Rect<float>& face) {
  constexpr float kThreshold = 0.05f;
  // Relax bottom bound for FPP detection since the ROI contains body points.
  return std::abs(roi.left - face.left) <= kThreshold &&
         std::abs(roi.top - face.top) <= kThreshold &&
         std::abs(roi.right() - face.right()) <= kThreshold &&
         roi.bottom() + kThreshold >= face.bottom();
}

bool PrepareStaticMetadata(android::CameraMetadata* static_info,
                           const Size& full_size,
                           const Size& stream_size,
                           float frame_rate) {
  const int32_t full_width = base::checked_cast<int32_t>(full_size.width);
  const int32_t full_height = base::checked_cast<int32_t>(full_size.height);
  const int32_t stream_width = base::checked_cast<int32_t>(stream_size.width);
  const int32_t stream_height = base::checked_cast<int32_t>(stream_size.height);
  const int64_t frame_duration_ns = static_cast<int32_t>(1e9f / frame_rate);

  const int32_t partial_result_count = 1;
  const int32_t active_array_size[] = {0, 0, full_width, full_height};
  const int32_t available_stream_configurations[] = {
      HAL_PIXEL_FORMAT_YCbCr_420_888,
      full_width,
      full_height,
      ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
      HAL_PIXEL_FORMAT_YCbCr_420_888,
      stream_width,
      stream_height,
      ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_OUTPUT,
  };
  const int64_t available_min_frame_durations[] = {
      HAL_PIXEL_FORMAT_YCbCr_420_888,
      full_width,
      full_height,
      frame_duration_ns,
      HAL_PIXEL_FORMAT_YCbCr_420_888,
      full_width,
      full_height,
      frame_duration_ns,
  };

  if (static_info->update(ANDROID_REQUEST_PARTIAL_RESULT_COUNT,
                          &partial_result_count, 1) != 0) {
    LOGF(ERROR) << "Failed to update ANDROID_REQUEST_PARTIAL_RESULT_COUNT";
    return false;
  }
  if (static_info->update(ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE,
                          active_array_size,
                          std::size(active_array_size)) != 0) {
    LOGF(ERROR) << "Failed to update ANDROID_SENSOR_INFO_ACTIVE_ARRAY_SIZE";
    return false;
  }
  if (static_info->update(ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                          available_stream_configurations,
                          std::size(available_stream_configurations)) != 0) {
    LOGF(ERROR)
        << "Failed to update ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS";
    return false;
  }
  if (static_info->update(ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS,
                          available_min_frame_durations,
                          std::size(available_min_frame_durations)) != 0) {
    LOGF(ERROR)
        << "Failed to update ANDROID_SCALER_AVAILABLE_MIN_FRAME_DURATIONS";
    return false;
  }
  return true;
}

bool PrepareResultMetadata(android::CameraMetadata* result_metadata,
                           int64_t sensor_timestamp,
                           const Rect<int32_t>& face_rect) {
  if (result_metadata->update(ANDROID_SENSOR_TIMESTAMP, &sensor_timestamp, 1) !=
      0) {
    LOGF(ERROR) << "Failed to update ANDROID_SENSOR_TIMESTAMP";
    return false;
  }
  const int32_t face_rectangles[] = {face_rect.left, face_rect.top,
                                     face_rect.right(), face_rect.bottom()};
  if (result_metadata->update(ANDROID_STATISTICS_FACE_RECTANGLES,
                              face_rectangles,
                              std::size(face_rectangles)) != 0) {
    LOGF(ERROR) << "Failed to update ANDROID_STATISTICS_FACE_RECTANGLES";
    return false;
  }
  return true;
}

}  // namespace

bool AutoFramingTestFixture::LoadTestImage(const base::FilePath& path) {
  test_image_one_face_ = TestImage::Create(path);
  if (!test_image_one_face_) {
    LOGF(ERROR) << "Failed to load test image from " << path;
    return false;
  }
  if (test_image_one_face_->face_rectangles().size() != 1) {
    LOGF(ERROR) << "Expected there's one face in the test image (metadata)";
    test_image_one_face_.reset();
    return false;
  }
  return true;
}

bool AutoFramingTestFixture::SetUp(
    const Size& full_size,
    const Size& stream_size,
    float frame_rate,
    std::vector<TestFrameInfo> input_frame_infos) {
  if (!PrepareStaticMetadata(&static_info_, full_size, stream_size,
                             frame_rate)) {
    return false;
  }

  runtime_options_ = StreamManipulator::RuntimeOptions{
      .auto_framing_state = mojom::CameraAutoFramingState::OFF,
  };
  auto_framing_stream_manipulator_ =
      std::make_unique<AutoFramingStreamManipulator>(&runtime_options_);

  const camera_metadata_t* locked_static_info = static_info_.getAndLock();
  if (!locked_static_info) {
    LOGF(ERROR) << "Failed to lock static info";
    return false;
  }
  if (!auto_framing_stream_manipulator_->Initialize(
          locked_static_info, /*result_callback=*/base::DoNothing())) {
    LOGF(ERROR) << "Failed to initialize AutoFramingStreamManipulator";
    return false;
  }
  if (static_info_.unlock(locked_static_info) != 0) {
    LOGF(ERROR) << "Failed to unlock static info";
    return false;
  }

  output_stream_ = camera3_stream_t{
      .stream_type = CAMERA3_STREAM_OUTPUT,
      .width = stream_size.width,
      .height = stream_size.height,
      .format = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED,
      .usage = 0,
  };
  output_streams_.push_back(&output_stream_);
  Camera3StreamConfiguration stream_config(camera3_stream_configuration_t{
      .num_streams = 1,
      .streams = output_streams_.data(),
      .operation_mode = CAMERA3_STREAM_CONFIGURATION_NORMAL_MODE,
  });
  if (!auto_framing_stream_manipulator_->ConfigureStreams(&stream_config)) {
    LOGF(ERROR) << "Failed to pre-configure streams";
    return false;
  }
  for (const camera3_stream_t* s : stream_config.GetStreams()) {
    if (s->width == full_size.width && s->height == full_size.height &&
        s->format == HAL_PIXEL_FORMAT_YCBCR_420_888) {
      if (input_stream_) {
        LOGF(ERROR) << "Found multiple framing input streams";
        return false;
      }
      input_stream_ = s;
    }
  }

  if (!auto_framing_stream_manipulator_->OnConfiguredStreams(&stream_config)) {
    LOGF(ERROR) << "Failed to post-configure streams";
    return false;
  }
  base::span<const camera3_stream_t* const> client_streams =
      stream_config.GetStreams();
  if (client_streams.size() != 1 || client_streams[0] != &output_stream_ ||
      client_streams[0]->width != stream_size.width ||
      client_streams[0]->height != stream_size.height) {
    LOGF(ERROR) << "Invalid post-configued streams";
    return false;
  }

  input_frame_infos_ = std::move(input_frame_infos);
  for (size_t i = 0; i < input_frame_infos_.size(); ++i) {
    input_buffers_.push_back(CreateTestFrameWithFace(
        input_stream_->width, input_stream_->height, input_stream_->format,
        input_stream_->usage, input_frame_infos_[i].face_rect));
    if (!input_buffers_.back()) {
      LOGF(ERROR) << "Failed to create input frame with face rect: "
                  << input_frame_infos_[i].face_rect.ToString();
      return false;
    }
  }
  output_buffer_ = CameraBufferManager::AllocateScopedBuffer(
      output_stream_.width, output_stream_.height, output_stream_.format,
      output_stream_.usage);
  if (!output_buffer_) {
    LOGF(ERROR) << "Failed to allocate output buffer";
    return false;
  }

  return true;
}

bool AutoFramingTestFixture::ProcessFrame(int64_t sensor_timestamp,
                                          bool is_enabled,
                                          bool* is_face_detected) {
  runtime_options_.auto_framing_state =
      is_enabled ? mojom::CameraAutoFramingState::ON_SINGLE
                 : mojom::CameraAutoFramingState::OFF;
  ++frame_number_;
  return ProcessCaptureRequest() &&
         ProcessCaptureResult(sensor_timestamp, is_face_detected);
}

ScopedBufferHandle AutoFramingTestFixture::CreateTestFrameWithFace(
    uint32_t width,
    uint32_t height,
    uint32_t format,
    uint32_t usage,
    const Rect<uint32_t>& face_rect) {
  if (!test_image_one_face_) {
    LOGF(ERROR) << "Test image is not loaded";
    return nullptr;
  }
  const Rect<float> src_rect = NormalizeRect(
      test_image_one_face_->face_rectangles()[0], test_image_one_face_->size());
  const Rect<float> dst_rect = NormalizeRect(face_rect, Size(width, height));
  const float scale_x = src_rect.width / dst_rect.width;
  const float scale_y = src_rect.height / dst_rect.height;
  const float offset_x = src_rect.left - dst_rect.left * scale_x;
  const float offset_y = src_rect.top - dst_rect.top * scale_y;
  if (scale_x > 1.0f || scale_y > 1.0f || offset_x < 0.0f || offset_y < 0.0f ||
      offset_x + scale_x > 1.0f || offset_y + scale_y > 1.0f) {
    LOGF(ERROR) << "Failed to create test frame with face at "
                << face_rect.ToString();
    return nullptr;
  }
  const Rect<uint32_t> crop(
      static_cast<uint32_t>(static_cast<float>(test_image_one_face_->width()) *
                            offset_x),
      static_cast<uint32_t>(static_cast<float>(test_image_one_face_->height()) *
                            offset_y),
      static_cast<uint32_t>(static_cast<float>(test_image_one_face_->width()) *
                            scale_x),
      static_cast<uint32_t>(static_cast<float>(test_image_one_face_->height()) *
                            scale_y));
  ScopedBufferHandle buffer =
      CameraBufferManager::AllocateScopedBuffer(width, height, format, usage);
  if (!buffer) {
    LOGF(ERROR) << "Failed to allocate buffer";
    return nullptr;
  }
  if (!WriteTestImageToBuffer(*test_image_one_face_, *buffer, crop)) {
    LOGF(ERROR) << "Failed to write test image to buffer";
    return nullptr;
  }
  return buffer;
}

bool AutoFramingTestFixture::ProcessCaptureRequest() {
  camera3_stream_buffer_t request_buffer = {
      .stream = &output_stream_,
      .buffer = output_buffer_.get(),
      .status = CAMERA3_BUFFER_STATUS_OK,
      .acquire_fence = -1,
      .release_fence = -1,
  };
  Camera3CaptureDescriptor request(camera3_capture_request_t{
      .frame_number = frame_number_,
      .settings = nullptr,
      .num_output_buffers = 1,
      .output_buffers = &request_buffer,
  });
  if (!auto_framing_stream_manipulator_->ProcessCaptureRequest(&request)) {
    LOGF(ERROR) << "Failed to process capture request";
    return false;
  }

  base::span<const camera3_stream_buffer_t> hal_buffers =
      request.GetOutputBuffers();
  switch (runtime_options_.auto_framing_state) {
    case mojom::CameraAutoFramingState::OFF:
      if (hal_buffers.size() != 1 || hal_buffers[0].stream != &output_stream_ ||
          hal_buffers[0].buffer != output_buffer_.get()) {
        LOGF(ERROR) << "Invalid processed capture request in OFF state";
        return false;
      }
      break;
    case mojom::CameraAutoFramingState::ON_SINGLE:
    case mojom::CameraAutoFramingState::ON_MULTI:
      if (hal_buffers.size() != 1 || hal_buffers[0].stream != input_stream_) {
        LOGF(ERROR) << "Invalid processed capture request in ON state";
        return false;
      }
      break;
  }

  return true;
}

bool AutoFramingTestFixture::ProcessCaptureResult(int64_t sensor_timestamp,
                                                  bool* is_face_detected) {
  const bool is_enabled =
      runtime_options_.auto_framing_state != mojom::CameraAutoFramingState::OFF;
  const size_t frame_index = GetFrameIndex(sensor_timestamp);
  if (!PrepareResultMetadata(
          &result_metadata_, sensor_timestamp,
          input_frame_infos_[frame_index].face_rect.AsRect<int32_t>())) {
    return false;
  }

  const camera_metadata_t* locked_result_metadata =
      result_metadata_.getAndLock();
  if (!locked_result_metadata) {
    LOGF(ERROR) << "Failed to lock result metadata";
    return false;
  }
  camera3_stream_buffer_t result_buffer = {
      .stream = is_enabled ? const_cast<camera3_stream_t*>(input_stream_)
                           : &output_stream_,
      // HACK: The input buffers allocated by the pipeline is replaced by our
      // pre-filled ones.
      .buffer =
          is_enabled ? input_buffers_[frame_index].get() : output_buffer_.get(),
      .status = CAMERA3_BUFFER_STATUS_OK,
      .acquire_fence = -1,
      .release_fence = -1,
  };
  Camera3CaptureDescriptor result(camera3_capture_result_t{
      .frame_number = frame_number_,
      .result = locked_result_metadata,
      .num_output_buffers = 1,
      .output_buffers = &result_buffer,
  });
  if (!auto_framing_stream_manipulator_->ProcessCaptureResult(&result)) {
    LOGF(ERROR) << "Failed to process capture result";
    return false;
  }
  if (result_metadata_.unlock(locked_result_metadata) != 0) {
    LOGF(ERROR) << "Failed to unlock result metadata";
    return false;
  }

  base::span<const camera3_stream_buffer_t> client_buffers =
      result.GetOutputBuffers();
  if (client_buffers.size() != 1 ||
      client_buffers[0].stream != &output_stream_) {
    LOGF(ERROR) << "Invalid processed capture result";
    return false;
  }

  if (is_enabled) {
    if (!IsAspectRatioMatched(
            auto_framing_stream_manipulator_->active_crop_region(),
            input_stream_->width, input_stream_->height, output_stream_.width,
            output_stream_.height)) {
      LOGF(ERROR)
          << "Crop window aspect ratio doesn't match the output: "
          << auto_framing_stream_manipulator_->active_crop_region().ToString();
      return false;
    }
    if (is_face_detected != nullptr) {
      *is_face_detected = IsRoiOnFace(
          auto_framing_stream_manipulator_->region_of_interest(),
          NormalizeRect(input_frame_infos_[frame_index].face_rect,
                        Size(input_stream_->width, input_stream_->height)));
    }
  } else {
    if (is_face_detected != nullptr) {
      *is_face_detected = false;
    }
  }

  return true;
}

size_t AutoFramingTestFixture::GetFrameIndex(int64_t sensor_timestamp) const {
  CHECK_GT(input_frame_infos_.size(), 0u);
  for (size_t i = 0; i < input_frame_infos_.size(); ++i) {
    if (sensor_timestamp <= input_frame_infos_[i].duration.InNanoseconds()) {
      return i;
    }
    sensor_timestamp -= input_frame_infos_[i].duration.InNanoseconds();
  }
  return input_frame_infos_.size() - 1;
}

}  // namespace cros::tests
