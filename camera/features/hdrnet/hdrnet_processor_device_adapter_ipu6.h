/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_HDRNET_HDRNET_PROCESSOR_DEVICE_ADAPTER_IPU6_H_
#define CAMERA_FEATURES_HDRNET_HDRNET_PROCESSOR_DEVICE_ADAPTER_IPU6_H_

#include "features/hdrnet/hdrnet_processor_device_adapter.h"

#include <memory>
#include <vector>

#include "gpu/gles/sampler.h"
#include "gpu/gles/screen_space_rect.h"
#include "gpu/gles/shader_program.h"

namespace cros {

// HdrNetProcessorDeviceAdapter implementation for Intel IPU6.
class HdrNetProcessorDeviceAdapterIpu6 : public HdrNetProcessorDeviceAdapter {
 public:
  HdrNetProcessorDeviceAdapterIpu6(
      const camera_metadata_t* static_info,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  // HdrNetProcessorDeviceAdapter implementations.
  ~HdrNetProcessorDeviceAdapterIpu6() override = default;
  bool Initialize() override;
  void TearDown() override;
  bool WriteRequestParameters(Camera3CaptureDescriptor* request,
                              MetadataLogger* metadata_logger) override;
  void ProcessResultMetadata(Camera3CaptureDescriptor* result,
                             MetadataLogger* metadata_logger) override;
  bool Preprocess(const HdrNetConfig::Options& options,
                  const SharedImage& input_yuv,
                  const SharedImage& output_rgba) override;
  bool Postprocess(const HdrNetConfig::Options& options,
                   const SharedImage& input_rgba,
                   const SharedImage& output_nv12) override;

 private:
  Texture2D CreateGainLutTexture(base::span<const float> tonemap_curve,
                                 bool inverse);

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  bool initialized_ = false;

  Texture2D gamma_lut_;
  Texture2D inverse_gamma_lut_;

  int num_curve_points_ = 0;
  Texture2D gtm_lut_;
  Texture2D inverse_gtm_lut_;
  std::vector<float> gtm_lut_buffer_;

  std::unique_ptr<ScreenSpaceRect> rect_;
  Sampler nearest_clamp_to_edge_;
  Sampler linear_clamp_to_edge_;
  ShaderProgram preprocessor_program_;
  ShaderProgram postprocessor_program_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_HDRNET_HDRNET_PROCESSOR_DEVICE_ADAPTER_IPU6_H_
