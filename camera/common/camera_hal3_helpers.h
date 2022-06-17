/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_CAMERA_HAL3_HELPERS_H_
#define CAMERA_COMMON_CAMERA_HAL3_HELPERS_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/containers/span.h>
#include <base/synchronization/lock.h>
#include <camera/camera_metadata.h>
#include <hardware/camera3.h>

#include "cros-camera/common.h"
#include "cros-camera/common_types.h"
#include "cros-camera/export.h"

#if USE_CAMERA_FEATURE_FACE_DETECTION
#include "cros-camera/camera_face_detection.h"
#endif

namespace cros {

// Utility function to produce a debug string for the given camera3_stream_t
// |stream|.
inline std::string GetDebugString(const camera3_stream_t* stream) {
  return base::StringPrintf(
      "stream=%p, type=%d, size=%ux%u, format=%d, usage=%u, max_buffers=%u",
      stream, stream->stream_type, stream->width, stream->height,
      stream->format, stream->usage, stream->max_buffers);
}

inline bool HaveSameAspectRatio(const camera3_stream_t* s1,
                                const camera3_stream_t* s2) {
  return (s1->width * s2->height == s1->height * s2->width);
}

template <typename T>
inline Rect<float> NormalizeRect(const Rect<T>& rect, const Size& size) {
  return Rect<float>(
      static_cast<float>(rect.left) / static_cast<float>(size.width),
      static_cast<float>(rect.top) / static_cast<float>(size.height),
      static_cast<float>(rect.width) / static_cast<float>(size.width),
      static_cast<float>(rect.height) / static_cast<float>(size.height));
}

template <typename T>
inline Rect<T> ClampRect(const Rect<T>& rect, const Rect<T>& bound) {
  const T left = std::clamp(rect.left, bound.left, bound.right());
  const T top = std::clamp(rect.top, bound.top, bound.bottom());
  const T right = std::clamp(rect.right(), bound.left, bound.right());
  const T bottom = std::clamp(rect.bottom(), bound.top, bound.bottom());
  return Rect<T>(left, top, right - left + 1, bottom - top + 1);
}

// Returns the maximum centering crop window within |size| with the specified
// aspect ratio.
Rect<uint32_t> GetCenteringFullCrop(Size size,
                                    uint32_t aspect_ratio_x,
                                    uint32_t aspect_ratio_y);

// A container for passing metadata across different StreamManipulator instances
// to allow different feature implementations to communicate with one another.
struct FeatureMetadata {
  // |hdr_ratio| produced by GcamAeStreamManipulator and consumed by
  // HdrNetStreamManipulator for HDRnet output frame rendering.
  std::optional<float> hdr_ratio;

#if USE_CAMERA_FEATURE_FACE_DETECTION
  // The face rectangles detected by the FaceDetectionStreamManipulator when
  // CrOS face detector is enabled. The coordinates of the rectangles are
  // normalized with respect to the active sensor array size. The face ROIs are
  // consumed by GcamAeStreamManipulator as input metadata.
  std::optional<std::vector<human_sensing::CrosFace>> faces;
#endif
};

// A helper class to make it easy to modify camera3_stream_configuration_t.
//
// The class is not thread-safe. The user of this class needs to ensure that the
// method calls are serialized and also that the class instance remains valid
// when the data members are being referenced externally.
class CROS_CAMERA_EXPORT Camera3StreamConfiguration {
 public:
  // Default constructor creates an invalid instance.
  Camera3StreamConfiguration() = default;
  explicit Camera3StreamConfiguration(
      const camera3_stream_configuration_t& stream_list);
  ~Camera3StreamConfiguration() = default;

  Camera3StreamConfiguration(Camera3StreamConfiguration&& other) = default;
  Camera3StreamConfiguration& operator=(Camera3StreamConfiguration&& other) =
      default;
  Camera3StreamConfiguration(const Camera3StreamConfiguration& other) = delete;
  Camera3StreamConfiguration& operator=(
      const Camera3StreamConfiguration& other) = delete;

  // Gets the stream configuration in a span.
  base::span<camera3_stream_t* const> GetStreams() const;

  // Sets the stream configuration to |streams|.
  bool SetStreams(base::span<camera3_stream_t* const> streams);

  // Appends |stream| to the stream configuration.
  bool AppendStream(camera3_stream_t* stream);

  // Locks the internal data and get the camera3_stream_configuration_t that can
  // be consumed by the Android HAL3 API.
  camera3_stream_configuration_t* Lock();

  // Unlocks the instance for further modification.
  void Unlock();

  bool is_valid() const { return !streams_.empty(); }
  uint32_t num_streams() const { return streams_.size(); }
  uint32_t operation_mode() const { return operation_mode_; }

 private:
  bool IsLocked() const;

  std::vector<camera3_stream_t*> streams_;
  uint32_t operation_mode_ = 0;
  const camera_metadata_t* session_parameters_ = nullptr;

  std::optional<camera3_stream_configuration_t> raw_configuration_;
};

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

  ~Camera3CaptureDescriptor();

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
  camera3_capture_request_t* GetLockedRequest();
  camera3_capture_result_t* GetLockedResult();

  // Unlocks the descriptor for further modification.
  void Unlock();

  bool is_valid() const { return type_ != Type::kInvalidType; }
  uint32_t frame_number() const { return frame_number_; }
  bool has_metadata() const { return !metadata_.isEmpty(); }
  uint32_t num_output_buffers() const { return output_buffers_.size(); }
  uint32_t partial_result() const { return partial_result_; }

  FeatureMetadata& feature_metadata() { return feature_metadata_; }

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

  FeatureMetadata feature_metadata_;

  union RawDescriptor {
    camera3_capture_request_t raw_request;
    camera3_capture_result_t raw_result;
  };
  std::optional<RawDescriptor> raw_descriptor_;
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
