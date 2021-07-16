/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_CAMERA_HAL3_HELPERS_H_
#define CAMERA_COMMON_CAMERA_HAL3_HELPERS_H_

#include <memory>
#include <vector>

#include <base/containers/span.h>
#include <base/synchronization/lock.h>
#include <camera/camera_metadata.h>
#include <hardware/camera3.h>

#include "cros-camera/common.h"
#include "cros-camera/export.h"

namespace cros {

// A helper class to make it easy to modify camera3_capture_request_t and
// camera3_capture_result_t objects.
//
// The class is not thread-safe. The user of this class needs to ensure that the
// method calls are serialized and also that the class instance remains valid
// when the data members are being referenced externally.
class CROS_CAMERA_EXPORT Camera3CaptureDescriptor {
 public:
  enum class Type {
    kInvalidType = -1,
    kCaptureRequest,
    kCaptureResult,
  };

  // Default constructor creates an invalid instance.
  Camera3CaptureDescriptor() = default;

  explicit Camera3CaptureDescriptor(const camera3_capture_request_t& request);
  explicit Camera3CaptureDescriptor(const camera3_capture_result_t& result);

  ~Camera3CaptureDescriptor() = default;

  Camera3CaptureDescriptor(Camera3CaptureDescriptor&& other);
  Camera3CaptureDescriptor& operator=(Camera3CaptureDescriptor&& other);
  Camera3CaptureDescriptor(const Camera3CaptureDescriptor& other) = delete;
  Camera3CaptureDescriptor& operator=(const Camera3CaptureDescriptor& other) =
      delete;

  // Metadata getter and setter. The templated methods only support the six data
  // types defined for Android camera_metadata_entry_t: uint8_t, int32_t, float,
  // double, int64_t, camera_metadata_rational_t.

  // Gets the metadata associated with |tag| as span. Returns empty span if
  // there's no metadata associated with |tag|.
  template <typename T>
  base::span<const T> GetMetadata(uint32_t tag) const;

  // Updates, and creates if not exist, the metadata associated with |tag| with
  // |values|. Returns true if the metadata is successfully updated; false
  // otherwise.
  template <typename T>
  bool UpdateMetadata(uint32_t tag, base::span<const T> values) {
    if (IsLocked()) {
      LOGF(ERROR) << "Cannot update metadata when locked";
      return false;
    }
    auto ret = metadata_.update(tag, values.data(), values.size());
    return ret == 0;
  }

  // Appends |metadata| to |metadata_|. Returns true if |metadata| is
  // successfully appended; false otherwise.
  bool AppendMetadata(const camera_metadata_t* metadata);

  // Deletes the metadata associated with |tag|. Returns true if the metadata is
  // successfully deleted; false otherwise.
  bool DeleteMetadata(uint32_t tag);

  // Sets the existing metadata by copying the contents from |metadata|.
  // Returns true if the metadata are set successfully; false otherwise.
  bool SetMetadata(const camera_metadata_t* metadata);

  // Getter and setter for the input buffer.
  const camera3_stream_buffer_t* GetInputBuffer() const;
  void SetInputBuffer(const camera3_stream_buffer_t& input_buffer);
  void ResetInputBuffer();

  // Getter and setter for the output buffers.
  base::span<const camera3_stream_buffer_t> GetOutputBuffers() const;
  void SetOutputBuffers(
      base::span<const camera3_stream_buffer_t> output_buffers);
  void AppendOutputBuffer(const camera3_stream_buffer_t& buffer);

  // Locks the internal data and get the raw camera3_capture_request_t /
  // camera3_capture_result_t that can be consumed by the Android HAL3 API.
  camera3_capture_request_t* LockForRequest();
  camera3_capture_result_t* LockForResult();

  // Unlocks the descriptor for further modification.
  void Unlock();

  bool is_valid() const { return type_ != Type::kInvalidType; }
  uint32_t frame_number() const { return frame_number_; }
  bool has_metadata() const { return !metadata_.isEmpty(); }
  uint32_t num_output_buffers() const { return output_buffers_.size(); }
  uint32_t partial_result() const { return partial_result_; }

 protected:
  void Invalidate();
  bool IsLocked() const;

  Type type_ = Type::kInvalidType;

  // Flattened data for both kCaptureRequest and kCaptureResult.
  uint32_t frame_number_ = 0;
  android::CameraMetadata metadata_;
  std::unique_ptr<camera3_stream_buffer_t> input_buffer_;
  std::vector<camera3_stream_buffer_t> output_buffers_;

  // For kCaptureResult only.
  uint32_t partial_result_ = 0;

  // The physical camera info are not being active used at the moment, so we
  // just use these fields to keep track of the original values.
  uint32_t num_physcam_metadata_ = 0;
  const char** physcam_ids_ = nullptr;
  const camera_metadata_t** physcam_metadata_ = nullptr;

  union RawDescriptor {
    camera3_capture_request_t raw_request;
    camera3_capture_result_t raw_result;
  };
  std::unique_ptr<RawDescriptor> raw_descriptor_;
};

template <>
CROS_CAMERA_EXPORT base::span<const uint8_t>
Camera3CaptureDescriptor::GetMetadata(uint32_t tag) const;

template <>
CROS_CAMERA_EXPORT base::span<const int32_t>
Camera3CaptureDescriptor::GetMetadata(uint32_t tag) const;

template <>
CROS_CAMERA_EXPORT base::span<const float>
Camera3CaptureDescriptor::GetMetadata(uint32_t tag) const;

template <>
CROS_CAMERA_EXPORT base::span<const double>
Camera3CaptureDescriptor::GetMetadata(uint32_t tag) const;

template <>
CROS_CAMERA_EXPORT base::span<const int64_t>
Camera3CaptureDescriptor::GetMetadata(uint32_t tag) const;

template <>
CROS_CAMERA_EXPORT base::span<const camera_metadata_rational_t>
Camera3CaptureDescriptor::GetMetadata(uint32_t tag) const;

}  // namespace cros

#endif  // CAMERA_COMMON_CAMERA_HAL3_HELPERS_H_
