/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/camera_hal3_helpers.h"

#include <algorithm>
#include <utility>

namespace cros {

//
// Camera3StreamConfiguration implementations.
//

Camera3StreamConfiguration::Camera3StreamConfiguration(
    const camera3_stream_configuration_t& stream_list)
    : streams_(stream_list.streams,
               stream_list.streams + stream_list.num_streams),
      operation_mode_(stream_list.operation_mode),
      session_parameters_(stream_list.session_parameters) {}

base::span<camera3_stream_t* const> Camera3StreamConfiguration::GetStreams()
    const {
  return {streams_.data(), streams_.size()};
}

bool Camera3StreamConfiguration::SetStreams(
    base::span<camera3_stream_t* const> streams) {
  if (IsLocked()) {
    LOGF(ERROR) << "Cannot set streams when locked";
    return false;
  }
  streams_.clear();
  streams_.resize(streams.size());
  std::copy(streams.begin(), streams.end(), streams_.begin());
  return true;
}

bool Camera3StreamConfiguration::AppendStream(camera3_stream_t* stream) {
  if (IsLocked()) {
    LOGF(ERROR) << "Cannot append streams when locked";
    return false;
  }
  streams_.push_back(stream);
  return true;
}

camera3_stream_configuration_t* Camera3StreamConfiguration::Lock() {
  CHECK(!IsLocked());
  raw_configuration_ = camera3_stream_configuration_t{
      .num_streams = static_cast<uint32_t>(streams_.size()),
      .streams = streams_.data(),
      .operation_mode = operation_mode_,
      .session_parameters = session_parameters_};
  return &raw_configuration_.value();
}

void Camera3StreamConfiguration::Unlock() {
  raw_configuration_.reset();
}

bool Camera3StreamConfiguration::IsLocked() const {
  return raw_configuration_.has_value();
}

//
// Camera3CaptureDescriptor implementations.
//

Camera3CaptureDescriptor::Camera3CaptureDescriptor(
    const camera3_capture_request_t& request)
    : type_(Type::kCaptureRequest),
      frame_number_(request.frame_number),
      output_buffers_(request.output_buffers,
                      request.output_buffers + request.num_output_buffers),
      num_physcam_metadata_(request.num_physcam_settings),
      physcam_ids_(request.physcam_id),
      physcam_metadata_(request.physcam_settings) {
  if (request.settings != nullptr) {
    metadata_.acquire(clone_camera_metadata(request.settings));
  }
  if (request.input_buffer) {
    input_buffer_ =
        std::make_unique<camera3_stream_buffer_t>(*request.input_buffer);
  }
}

Camera3CaptureDescriptor::Camera3CaptureDescriptor(
    const camera3_capture_result_t& result)
    : type_(Type::kCaptureResult),
      frame_number_(result.frame_number),
      output_buffers_(result.output_buffers,
                      result.output_buffers + result.num_output_buffers),
      partial_result_(result.partial_result),
      num_physcam_metadata_(result.num_physcam_metadata),
      physcam_ids_(result.physcam_ids),
      physcam_metadata_(result.physcam_metadata) {
  if (result.result != nullptr) {
    metadata_.acquire(clone_camera_metadata(result.result));
  }
  if (result.input_buffer) {
    input_buffer_ =
        std::make_unique<camera3_stream_buffer_t>(*result.input_buffer);
  }
}

Camera3CaptureDescriptor::~Camera3CaptureDescriptor() {
  Unlock();
  Invalidate();
}

Camera3CaptureDescriptor::Camera3CaptureDescriptor(
    Camera3CaptureDescriptor&& other) {
  *this = std::move(other);
}

Camera3CaptureDescriptor& Camera3CaptureDescriptor::operator=(
    Camera3CaptureDescriptor&& other) {
  if (this != &other) {
    type_ = other.type_;
    frame_number_ = other.frame_number_;
    if (!other.metadata_.isEmpty()) {
      metadata_.acquire(other.metadata_.release());
    }
    input_buffer_ = std::move(other.input_buffer_);
    output_buffers_ = std::move(other.output_buffers_);
    partial_result_ = other.partial_result_;
    num_physcam_metadata_ = other.num_physcam_metadata_;
    physcam_ids_ = other.physcam_ids_;
    physcam_metadata_ = other.physcam_metadata_;
    feature_metadata_ = std::move(other.feature_metadata_);
    raw_descriptor_ = std::move(other.raw_descriptor_);

    other.Invalidate();
  }
  return *this;
}

template <>
base::span<const uint8_t> Camera3CaptureDescriptor::GetMetadata(
    uint32_t tag) const {
  camera_metadata_ro_entry_t entry = metadata_.find(tag);
  if (entry.count == 0) {
    return base::span<const uint8_t>();
  }
  return base::span<const uint8_t>(entry.data.u8, entry.count);
}

template <>
base::span<const int32_t> Camera3CaptureDescriptor::GetMetadata(
    uint32_t tag) const {
  camera_metadata_ro_entry_t entry = metadata_.find(tag);
  if (entry.count == 0) {
    return base::span<const int32_t>();
  }
  return base::span<const int32_t>(entry.data.i32, entry.count);
}

template <>
base::span<const float> Camera3CaptureDescriptor::GetMetadata(
    uint32_t tag) const {
  camera_metadata_ro_entry_t entry = metadata_.find(tag);
  if (entry.count == 0) {
    return base::span<const float>();
  }
  return base::span<const float>(entry.data.f, entry.count);
}

template <>
base::span<const double> Camera3CaptureDescriptor::GetMetadata(
    uint32_t tag) const {
  camera_metadata_ro_entry_t entry = metadata_.find(tag);
  if (entry.count == 0) {
    return base::span<const double>();
  }
  return base::span<const double>(entry.data.d, entry.count);
}

template <>
base::span<const int64_t> Camera3CaptureDescriptor::GetMetadata(
    uint32_t tag) const {
  camera_metadata_ro_entry_t entry = metadata_.find(tag);
  if (entry.count == 0) {
    return base::span<const int64_t>();
  }
  return base::span<const int64_t>(entry.data.i64, entry.count);
}

template <>
base::span<const camera_metadata_rational_t>
Camera3CaptureDescriptor::GetMetadata(uint32_t tag) const {
  camera_metadata_ro_entry_t entry = metadata_.find(tag);
  if (entry.count == 0) {
    return base::span<const camera_metadata_rational_t>();
  }
  return base::span<const camera_metadata_rational_t>(entry.data.r,
                                                      entry.count);
}

bool Camera3CaptureDescriptor::AppendMetadata(
    const camera_metadata_t* metadata) {
  if (IsLocked()) {
    LOGF(ERROR) << "Cannot update metadata when locked";
    return false;
  }
  auto ret = metadata_.append(metadata);
  return ret == 0;
}

bool Camera3CaptureDescriptor::DeleteMetadata(uint32_t tag) {
  if (IsLocked()) {
    LOGF(ERROR) << "Cannot delete metadata when locked";
    return false;
  }
  auto ret = metadata_.erase(tag);
  return ret == 0;
}

bool Camera3CaptureDescriptor::SetMetadata(const camera_metadata_t* metadata) {
  if (IsLocked()) {
    LOGF(ERROR) << "Cannot set metadata when locked";
    return false;
  }
  if (get_camera_metadata_entry_count(metadata) == 0) {
    LOGF(ERROR) << "The input metadata is empty";
    return false;
  }
  metadata_.acquire(clone_camera_metadata(metadata));
  return !metadata_.isEmpty();
}

const camera3_stream_buffer_t* Camera3CaptureDescriptor::GetInputBuffer()
    const {
  return input_buffer_.get();
}

void Camera3CaptureDescriptor::SetInputBuffer(
    const camera3_stream_buffer_t& input_buffer) {
  input_buffer_ = std::make_unique<camera3_stream_buffer_t>(input_buffer);
}

void Camera3CaptureDescriptor::ResetInputBuffer() {
  input_buffer_ = nullptr;
}

base::span<const camera3_stream_buffer_t>
Camera3CaptureDescriptor::GetOutputBuffers() const {
  return {output_buffers_.data(), output_buffers_.size()};
}

void Camera3CaptureDescriptor::SetOutputBuffers(
    base::span<const camera3_stream_buffer_t> output_buffers) {
  output_buffers_.clear();
  output_buffers_.resize(output_buffers.size());
  std::copy(output_buffers.begin(), output_buffers.end(),
            output_buffers_.begin());
}

void Camera3CaptureDescriptor::AppendOutputBuffer(
    const camera3_stream_buffer_t& buffer) {
  output_buffers_.push_back(buffer);
}

camera3_capture_request* Camera3CaptureDescriptor::LockForRequest() {
  if (type_ != Type::kCaptureRequest) {
    LOGF(ERROR) << "Cannot lock for capture request";
    return nullptr;
  }
  CHECK(!IsLocked());
  raw_descriptor_ = RawDescriptor();
  raw_descriptor_->raw_request.frame_number = frame_number_;
  raw_descriptor_->raw_request.settings = metadata_.getAndLock();
  raw_descriptor_->raw_request.input_buffer = input_buffer_.get();
  raw_descriptor_->raw_request.num_output_buffers = output_buffers_.size();
  raw_descriptor_->raw_request.output_buffers = output_buffers_.data();
  raw_descriptor_->raw_request.num_physcam_settings = num_physcam_metadata_;
  raw_descriptor_->raw_request.physcam_id = physcam_ids_;
  raw_descriptor_->raw_request.physcam_settings = physcam_metadata_;

  return &raw_descriptor_->raw_request;
}

camera3_capture_result_t* Camera3CaptureDescriptor::LockForResult() {
  if (type_ != Type::kCaptureResult) {
    LOGF(ERROR) << "Cannot lock for capture result";
    return nullptr;
  }
  CHECK(!IsLocked());
  raw_descriptor_ = RawDescriptor();
  raw_descriptor_->raw_result.frame_number = frame_number_;
  raw_descriptor_->raw_result.result = metadata_.getAndLock();
  raw_descriptor_->raw_result.num_output_buffers = output_buffers_.size();
  raw_descriptor_->raw_result.output_buffers = output_buffers_.data();
  raw_descriptor_->raw_result.input_buffer = input_buffer_.get();
  raw_descriptor_->raw_result.partial_result = partial_result_;
  raw_descriptor_->raw_result.num_physcam_metadata = num_physcam_metadata_;
  raw_descriptor_->raw_result.physcam_ids = physcam_ids_;
  raw_descriptor_->raw_result.physcam_metadata = physcam_metadata_;

  return &raw_descriptor_->raw_result;
}

camera3_capture_request_t* Camera3CaptureDescriptor::GetLockedRequest() {
  if (type_ != Type::kCaptureRequest) {
    LOGF(ERROR) << "Cannot lock for capture request";
    return nullptr;
  }
  if (!IsLocked()) {
    return nullptr;
  }
  return &raw_descriptor_->raw_request;
}

camera3_capture_result_t* Camera3CaptureDescriptor::GetLockedResult() {
  if (type_ != Type::kCaptureResult) {
    LOGF(ERROR) << "Cannot lock for capture result";
    return nullptr;
  }
  if (!IsLocked()) {
    return nullptr;
  }
  return &raw_descriptor_->raw_result;
}

void Camera3CaptureDescriptor::Unlock() {
  if (!is_valid() || !IsLocked()) {
    return;
  }
  switch (type_) {
    case Type::kCaptureRequest:
      metadata_.unlock(raw_descriptor_->raw_request.settings);
      break;
    case Type::kCaptureResult:
      metadata_.unlock(raw_descriptor_->raw_result.result);
      break;
    case Type::kInvalidType:
      NOTREACHED() << "Cannot unlock invalid descriptor";
  }
  raw_descriptor_.reset();
}

void Camera3CaptureDescriptor::Invalidate() {
  type_ = Type::kInvalidType;
  frame_number_ = 0;
  metadata_.clear();
  input_buffer_ = nullptr;
  output_buffers_.clear();
  partial_result_ = 0;
  physcam_ids_ = nullptr;
  physcam_metadata_ = nullptr;
  raw_descriptor_.reset();
}

bool Camera3CaptureDescriptor::IsLocked() const {
  return raw_descriptor_.has_value();
}

}  // namespace cros
