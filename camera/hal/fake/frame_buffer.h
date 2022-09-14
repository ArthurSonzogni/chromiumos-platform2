/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_FAKE_FRAME_BUFFER_H_
#define CAMERA_HAL_FAKE_FRAME_BUFFER_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include <base/sequence_checker.h>

#include "cros-camera/camera_buffer_manager.h"

namespace cros {

// FrameBuffer uses CameraBufferManager to manage the buffer.
// The class is not thread safe and all methods should be run on the same
// sequence.
class FrameBuffer {
 public:
  enum {
    kYPlane = 0,
    kUPlane = 1,
    kVPlane = 2,
  };

  // Returns true if buffer is already mapped. Otherwise, if mapped
  // successfully, the address will be assigned to |data_| and return true.
  // Otherwise, returns false.
  bool Map();

  // Unmaps the mapped address if it's mapped. Returns true if buffer is not
  // mapped or unmapping succeed.
  bool Unmap();

  // Gets the data pointer to the plane. Returns nullptr if the buffer is not
  // mapped or the given plane index is out of range.
  uint8_t* GetData(size_t plane = 0) const;

  // Gets the stride to the plane. Returns 0 if the buffer is not mapped or the
  // given plane index is out of range.
  size_t GetStride(size_t plane = 0) const;

  // Gets the plane size to the plane. Returns 0 if the buffer is not mapped or
  // the given plane index is out of range.
  size_t GetPlaneSize(size_t plane = 0) const;

  uint32_t GetWidth() const { return width_; }
  uint32_t GetHeight() const { return height_; }
  uint32_t GetFourcc() const { return fourcc_; }
  size_t GetNumPlanes() const { return data_.size(); }

  ~FrameBuffer();

  buffer_handle_t GetBufferHandle() const { return buffer_; }

  // Wraps external buffer from upper framework. Fill |width_| and |height_|
  // according to the parameters. Returns nullptr when there's error.
  static std::unique_ptr<FrameBuffer> Wrap(buffer_handle_t buffer,
                                           uint32_t width,
                                           uint32_t height);

  // Allocates the buffer internally. Returns nullptr when there's error.
  static std::unique_ptr<FrameBuffer> Create(uint32_t width,
                                             uint32_t height,
                                             android_pixel_format_t fourcc);

 private:
  FrameBuffer();

  // Wraps external buffer from upper framework. Fill |width_| and |height_|
  // according to the parameters.
  bool Initialize(buffer_handle_t buffer, uint32_t width, uint32_t height);

  // Allocate the buffer internally.
  bool Initialize(uint32_t width,
                  uint32_t height,
                  android_pixel_format_t fourcc);

  // Data pointer to each plane. Always have the size of number of planes, but
  // contains all nullptr when the buffer is not mapped.
  std::vector<uint8_t*> data_;

  // Stride of each plane. Always have the size of number of planes, but
  // contains all 0 when buffer is not mapped.
  std::vector<size_t> stride_;

  // Frame resolution.
  uint32_t width_;
  uint32_t height_;

  // This is V4L2_PIX_FMT_* in linux/videodev2.h.
  uint32_t fourcc_;

  // The currently used buffer.
  buffer_handle_t buffer_ = nullptr;

  // Used to import gralloc buffer.
  CameraBufferManager* buffer_manager_;

  // Whether the |buffer_| is allocated by this class.
  bool is_buffer_owned_ = false;

  bool is_mapped_ = false;

  // Use to check all methods are called on the same thread.
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace cros

#endif  // CAMERA_HAL_FAKE_FRAME_BUFFER_H_
