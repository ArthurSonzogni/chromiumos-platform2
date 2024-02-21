// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_FEATURES_SUPER_RESOLUTION_SINGLE_FRAME_UPSAMPLER_H_
#define CAMERA_FEATURES_SUPER_RESOLUTION_SINGLE_FRAME_UPSAMPLER_H_

#include <memory>
#include <optional>

#include <base/files/scoped_file.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/upsample_wrapper.h"

namespace cros {

class SingleFrameUpsampler {
 public:
  SingleFrameUpsampler() = default;
  SingleFrameUpsampler(const SingleFrameUpsampler&) = delete;
  SingleFrameUpsampler& operator=(const SingleFrameUpsampler&) = delete;

  bool Initialize();

  std::optional<base::ScopedFD> ProcessRequest(buffer_handle_t input_buffer,
                                               buffer_handle_t output_buffer,
                                               base::ScopedFD release_fence);

 private:
  bool ConvertNV12ToRGB(const ScopedMapping& in_mapping,
                        uint8_t* rgb_buf_addr,
                        uint32_t rgb_buf_stride);

  bool ConvertRGBToNV12(const uint8_t* rgb_buf_addr,
                        uint32_t rgb_buf_stride,
                        const ScopedMapping& out_mapping);

  std::unique_ptr<UpsampleWrapper> lancet_runner_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_SUPER_RESOLUTION_SINGLE_FRAME_UPSAMPLER_H_
