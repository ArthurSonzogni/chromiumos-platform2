// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "features/kiosk_vision/kiosk_vision_wrapper.h"

#include <libyuv.h>
#include <linux/videodev2.h>

#include <cros-camera/common.h>
#include <cros-camera/libkioskvision/kiosk_audience_measurement_bindings.h>

#include "features/kiosk_vision/kiosk_vision_library.h"

namespace cros {

namespace {

// Converts C++ class method to C function pointer.
template <typename T>
struct CCallbackHelper;

template <typename Ret, typename... Params>
struct CCallbackHelper<Ret(Params...)> {
  template <typename... Args>
  static Ret callback(Args... args) {
    return func(args...);
  }
  static std::function<Ret(Params...)> func;
};

// Initializes the static member.
template <typename Ret, typename... Params>
std::function<Ret(Params...)> CCallbackHelper<Ret(Params...)>::func;

cros_kiosk_vision_OnFrameProcessedCallbackFn convertFrameMethodToCCallback(
    KioskVisionWrapper* ptr) {
  CCallbackHelper<void(int64_t, const cros::kiosk_vision::Appearance*,
                       uint32_t)>::func =
      std::bind_front(&KioskVisionWrapper::OnFrameProcessed, ptr);
  return static_cast<cros_kiosk_vision_OnFrameProcessedCallbackFn>(
      CCallbackHelper<void(int64_t, const cros::kiosk_vision::Appearance*,
                           uint32_t)>::callback);
}

cros_kiosk_vision_OnTrackCompletedCallbackFn convertTrackMethodToCCallback(
    KioskVisionWrapper* ptr) {
  CCallbackHelper<void(int32_t, const cros::kiosk_vision::Appearance*, uint32_t,
                       int64_t, int64_t)>::func =
      std::bind_front(&KioskVisionWrapper::OnTrackCompleted, ptr);
  return static_cast<cros_kiosk_vision_OnTrackCompletedCallbackFn>(
      CCallbackHelper<void(int32_t, const cros::kiosk_vision::Appearance*,
                           uint32_t, int64_t, int64_t)>::callback);
}

cros_kiosk_vision_OnErrorCallbackFn convertErrorMethodToCCallback(
    KioskVisionWrapper* ptr) {
  CCallbackHelper<void()>::func =
      std::bind_front(&KioskVisionWrapper::OnError, ptr);
  return static_cast<cros_kiosk_vision_OnErrorCallbackFn>(
      CCallbackHelper<void()>::callback);
}

}  // namespace

KioskVisionWrapper::~KioskVisionWrapper() {
  auto delete_fn = KioskVisionLibrary::Get().delete_fn();
  if (pipeline_handle_ && delete_fn) {
    delete_fn(pipeline_handle_);
  }
}

bool KioskVisionWrapper::Initialize(const base::FilePath& dlc_root_path) {
  return InitializeLibrary(dlc_root_path) && InitializePipeline() &&
         InitializeInputBuffer();
}

bool KioskVisionWrapper::InitializeLibrary(
    const base::FilePath& dlc_root_path) {
  KioskVisionLibrary::Load(dlc_root_path);
  if (!KioskVisionLibrary::IsLoaded()) {
    LOGF(ERROR) << "Cannot create Kiosk Vision pipeline. Failed to load Kiosk "
                   "Vision library";
    return false;
  }
  return true;
}

bool KioskVisionWrapper::InitializePipeline() {
  cros_kiosk_vision_OnFrameProcessedCallbackFn c_frame_callback =
      convertFrameMethodToCCallback(this);
  cros_kiosk_vision_OnTrackCompletedCallbackFn c_track_callback =
      convertTrackMethodToCCallback(this);
  cros_kiosk_vision_OnErrorCallbackFn c_error_callback =
      convertErrorMethodToCCallback(this);

  auto create_fn = KioskVisionLibrary::Get().create_fn();
  create_fn(c_frame_callback, c_track_callback, c_error_callback,
            &pipeline_handle_);

  if (!pipeline_handle_) {
    LOGF(ERROR) << "Cannot create Kiosk Vision pipeline. Empty handle result";
    return false;
  }
  return true;
}

bool KioskVisionWrapper::InitializeInputBuffer() {
  cros::kiosk_vision::ImageSize size;
  cros::kiosk_vision::ImageFormat format;
  auto get_properties_fn = KioskVisionLibrary::Get().get_properties_fn();
  get_properties_fn(pipeline_handle_, &size, &format);

  detector_input_width_ = size.width;
  detector_input_height_ = size.height;
  LOGF(INFO) << "Kiosk Vision detector input: " << detector_input_width_ << "x"
             << detector_input_height_;
  if (detector_input_width_ <= 0 || detector_input_height_ <= 0) {
    LOGF(ERROR) << "Cannot prepare Kiosk Vision pipeline. Bad detector size";
    return false;
  }

  detector_input_buffer_.resize(detector_input_width_ * detector_input_height_);
  return true;
}

bool KioskVisionWrapper::ProcessFrame(int64_t timestamp,
                                      buffer_handle_t buffer) {
  ScopedMapping mapping(buffer);

  if (mapping.v4l2_format() != V4L2_PIX_FMT_NV12) {
    LOGF(ERROR) << "Unsupported input format "
                << FormatToString(mapping.v4l2_format());
    return false;
  }

  libyuv::ScalePlane(
      mapping.plane(0).addr, mapping.plane(0).stride, mapping.width(),
      mapping.height(), detector_input_buffer_.data(), detector_input_width_,
      detector_input_width_, detector_input_height_, libyuv::kFilterNone);

  cros::kiosk_vision::InputFrame input_frame{
      {detector_input_width_, detector_input_height_},
      cros::kiosk_vision::ImageFormat::kGray8,
      detector_input_buffer_.data(),
      detector_input_width_};

  auto process_frame_fn = KioskVisionLibrary::Get().process_frame_fn();
  auto status = process_frame_fn(pipeline_handle_, timestamp, &input_frame);

  if (status != CROS_KIOSK_VISION_OK) {
    LOGF(ERROR) << "Kiosk Vision pipeline failed to process frame at timestamp "
                << timestamp;
    return false;
  }
  return true;
}

void KioskVisionWrapper::OnFrameProcessed(
    cros::kiosk_vision::Timestamp timestamp,
    const cros::kiosk_vision::Appearance* audience_data,
    uint32_t audience_size) {
  // TODO(sbykov): Pass current objects to stream manipulator.
}

void KioskVisionWrapper::OnTrackCompleted(
    cros::kiosk_vision::TrackID id,
    const cros::kiosk_vision::Appearance* audience_data,
    uint32_t audience_size,
    cros::kiosk_vision::Timestamp start_time,
    cros::kiosk_vision::Timestamp end_time) {
  // TODO(sbykov): Process completed track (if needed).
}

void KioskVisionWrapper::OnError() {
  // TODO(sbykov): Process error state of the algorithm.
  // Reject new calls or add logic to reset the pipeline.
}

}  // namespace cros
