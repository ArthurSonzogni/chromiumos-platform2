/*
 * Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_REPROCESS_EFFECT_REPROCESS_EFFECT_H_
#define CAMERA_HAL_ADAPTER_REPROCESS_EFFECT_REPROCESS_EFFECT_H_

#include <string>
#include <utility>
#include <vector>

#include <base/functional/callback.h>
#include <camera/camera_metadata.h>
#include <hardware/gralloc.h>
#include <system/camera_metadata.h>

#include "cros-camera/camera_mojo_channel_manager_token.h"

struct VendorTagInfo {
  const char* name;
  uint8_t type;
  union {
    uint8_t u8;
    int32_t i32;
    float f;
    int64_t i64;
    double d;
    camera_metadata_rational_t r;
  } data;
};

namespace cros {

class ReprocessEffect {
 public:
  // Initializes the reprocessing effect and get the vendor tags the effect
  // requests.
  // Args:
  //    |request_vendor_tags|: names and types of request vendor tags
  //    |result_vendor_tags|: names and types of result vendor tags
  //    |token|: the mojo manager token
  // Returns:
  //    0 on success; corresponding error code on failure.
  virtual int32_t InitializeAndGetVendorTags(
      std::vector<VendorTagInfo>* request_vendor_tags,
      std::vector<VendorTagInfo>* result_vendor_tags,
      CameraMojoChannelManagerToken* token) = 0;

  // Sets the vendor tags that are allocated for the reprocessing effects.
  // Args:
  //    |request_vendor_tag_start|: start value of allocated request vendor tags
  //    |request_vendor_tag_count|: number of allocated request vendor tags
  //    |result_vendor_tag_start|: start value of allocated result vendor tags
  //    |result_vendor_tag_count|: number of allocated result vendor tags
  // Returns:
  //    0 on success; corresponding error code on failure.
  virtual int32_t SetVendorTags(uint32_t request_vendor_tag_start,
                                uint32_t request_vendor_tag_count,
                                uint32_t result_vendor_tag_start,
                                uint32_t result_vendor_tag_count) = 0;

  // Applies the reprocessing effect. Currently it is assumed that all effects
  // have the same output resolution and format as that of input.
  // Args:
  //    |settings|: input metadata settings
  //    |input_buffer|: input buffer
  //    |orientation|: clockwise rotation angle in degrees to be viewed upright
  //    |result_metadata|: output result metadata
  //    |output_buffer|: output buffer
  // Returns:
  //    0 on success; corresponding error code on failure.
  virtual int32_t ReprocessRequest(const camera_metadata_t& settings,
                                   buffer_handle_t input_buffer,
                                   uint32_t orientation,
                                   android::CameraMetadata* result_metadata,
                                   buffer_handle_t output_buffer) = 0;
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_REPROCESS_EFFECT_REPROCESS_EFFECT_H_
