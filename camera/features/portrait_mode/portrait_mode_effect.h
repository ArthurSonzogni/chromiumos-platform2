/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_PORTRAIT_MODE_PORTRAIT_MODE_EFFECT_H_
#define CAMERA_FEATURES_PORTRAIT_MODE_PORTRAIT_MODE_EFFECT_H_

#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/process/process.h>
#include <base/synchronization/condition_variable.h>
#include <base/synchronization/lock.h>
#include <base/threading/thread.h>
#include <camera/camera_metadata.h>

#include "camera/mojo/camera_features.mojom.h"
#include "common/vendor_tag_manager.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/portrait_cros_wrapper.h"

namespace cros {

// Vendor tag to indicate whether CrOS portrait mode can be attempted.
constexpr char kPortraitModeVendorTagSectionName[] = "com.google";

// 1: enable portrait processing
// 0: disable portrait processing
constexpr char kPortraitModeVendorTagName[] = "com.google.effect.portraitMode";
// The status of mojom::PortraitModeSegResult.
constexpr char kPortraitModeResultVendorTagName[] =
    "com.google.effect.portraitModeSegmentationResult";

constexpr uint32_t kPortraitModeVendorKey = kPortraitModeVendorTagStart;
constexpr uint32_t kPortraitModeSegmentationResultVendorKey =
    kPortraitModeVendorTagStart + 1;

class PortraitModeEffect {
 public:
  PortraitModeEffect();
  ~PortraitModeEffect();
  PortraitModeEffect(const PortraitModeEffect&) = delete;
  PortraitModeEffect& operator=(const PortraitModeEffect&) = delete;

  // Applies the portrait mode effect. Currently it is assumed that the effect
  // have the same output resolution and format as that of input.
  // Args:
  //    |input_buffer|: input buffer
  //    |orientation|: clockwise rotation angle in degrees to be viewed upright
  //    |segmentation_result|: portrait mode segmentation result
  //    |output_buffer|: output buffer
  // Returns:
  //    0 on success; corresponding error code on failure.
  int32_t ProcessRequest(buffer_handle_t input_buffer,
                         uint32_t orientation,
                         mojom::PortraitModeSegResult* segmentation_result,
                         buffer_handle_t output_buffer);

 private:
  void UpdateSegmentationResult(
      mojom::PortraitModeSegResult* segmentation_result, const int* result);

  int ConvertYUVToRGB(const ScopedMapping& mapping,
                      void* rgb_buf_addr,
                      uint32_t rgb_buf_stride);

  int ConvertRGBToYUV(void* rgb_buf_addr,
                      uint32_t rgb_buf_stride,
                      const ScopedMapping& mapping);

  void ProcessRequestAsync(
      buffer_handle_t input_buffer,
      buffer_handle_t output_buffer,
      int orientation,
      base::OnceCallback<void(int32_t)> task_completed_callback);

  CameraBufferManager* buffer_manager_;

  creative_camera::PortraitCrosWrapper portrait_processor_;
  uint32_t req_id_ = 0;
  base::Thread thread_;
  bool portrait_processor_init = false;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_PORTRAIT_MODE_PORTRAIT_MODE_EFFECT_H_
