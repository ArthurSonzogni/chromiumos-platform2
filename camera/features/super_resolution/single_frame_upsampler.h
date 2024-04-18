// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_FEATURES_SUPER_RESOLUTION_SINGLE_FRAME_UPSAMPLER_H_
#define CAMERA_FEATURES_SUPER_RESOLUTION_SINGLE_FRAME_UPSAMPLER_H_

#include <memory>
#include <optional>

#include <base/files/file_path.h>
#include <base/files/scoped_file.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/libupsample/upsample_wrapper_types.h"

namespace cros {

class SingleFrameUpsampler {
 public:
  SingleFrameUpsampler() = default;
  ~SingleFrameUpsampler();
  SingleFrameUpsampler(const SingleFrameUpsampler&) = delete;
  SingleFrameUpsampler& operator=(const SingleFrameUpsampler&) = delete;

  bool Initialize(const base::FilePath& dlc_root_path);

  std::optional<base::ScopedFD> ProcessRequest(buffer_handle_t input_buffer,
                                               buffer_handle_t output_buffer,
                                               base::ScopedFD release_fence,
                                               ResamplingMethod method,
                                               bool use_lancet_alpha);

 private:
  bool ConvertNV12ToRGB(const ScopedMapping& in_mapping,
                        uint8_t* rgb_buf_addr,
                        uint32_t rgb_buf_stride);

  bool ConvertRGBToNV12(const uint8_t* rgb_buf_addr,
                        uint32_t rgb_buf_stride,
                        const ScopedMapping& out_mapping);

  // Function pointer to the upsampling algorithm implementation loaded from the
  // upsampler library.
  void* lancet_runner_ = nullptr;
  void* lancet_alpha_runner_ = nullptr;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_SUPER_RESOLUTION_SINGLE_FRAME_UPSAMPLER_H_
