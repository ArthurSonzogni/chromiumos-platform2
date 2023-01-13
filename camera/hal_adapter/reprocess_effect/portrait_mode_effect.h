/*
 * Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_REPROCESS_EFFECT_PORTRAIT_MODE_EFFECT_H_
#define CAMERA_HAL_ADAPTER_REPROCESS_EFFECT_PORTRAIT_MODE_EFFECT_H_

#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/synchronization/condition_variable.h>
#include <base/synchronization/lock.h>
#include <base/process/process.h>

#include "cros-camera/camera_buffer_manager.h"
#include "hal_adapter/reprocess_effect/gpu_algo_manager.h"
#include "hal_adapter/reprocess_effect/reprocess_effect.h"

namespace cros {

class PortraitModeEffect : public ReprocessEffect,
                           public base::SupportsWeakPtr<PortraitModeEffect> {
 public:
  PortraitModeEffect();
  PortraitModeEffect(const PortraitModeEffect&) = delete;
  PortraitModeEffect& operator=(const PortraitModeEffect&) = delete;

  virtual ~PortraitModeEffect() = default;

  int32_t InitializeAndGetVendorTags(
      std::vector<VendorTagInfo>* request_vendor_tags,
      std::vector<VendorTagInfo>* result_vendor_tags,
      CameraMojoChannelManagerToken* token) override;

  int32_t SetVendorTags(uint32_t request_vendor_tag_start,
                        uint32_t request_vendor_tag_count,
                        uint32_t result_vendor_tag_start,
                        uint32_t result_vendor_tag_count) override;

  int32_t ReprocessRequest(const camera_metadata_t& settings,
                           buffer_handle_t input_buffer,
                           uint32_t orientation,
                           android::CameraMetadata* result_metadata,
                           buffer_handle_t output_buffer) override;

 private:
  void UpdateResultMetadata(android::CameraMetadata* result_metadata,
                            const int* result);

  int ConvertYUVToRGB(const ScopedMapping& mapping,
                      void* rgb_buf_addr,
                      uint32_t rgb_buf_stride);

  int ConvertRGBToYUV(void* rgb_buf_addr,
                      uint32_t rgb_buf_stride,
                      const ScopedMapping& mapping);

  void ReturnCallback(uint32_t status, int32_t buffer_handle);

  uint32_t enable_vendor_tag_;

  enum class SegmentationResult : uint8_t { kSuccess, kFailure, kTimeout };

  uint32_t result_vendor_tag_;

  CameraBufferManager* buffer_manager_;

  GPUAlgoManager* gpu_algo_manager_;

  base::Lock lock_;

  base::ConditionVariable condvar_;

  int32_t return_status_;
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_REPROCESS_EFFECT_PORTRAIT_MODE_EFFECT_H_
