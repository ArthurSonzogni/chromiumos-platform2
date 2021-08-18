/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_GPU_IMAGE_PROCESSOR_H_
#define CAMERA_GPU_IMAGE_PROCESSOR_H_

#include "gpu/gles/sampler.h"
#include "gpu/gles/screen_space_rect.h"
#include "gpu/gles/shader_program.h"
#include "gpu/gles/texture_2d.h"

namespace cros {

class GpuImageProcessor {
 public:
  GpuImageProcessor();

  GpuImageProcessor(const GpuImageProcessor& other) = delete;
  GpuImageProcessor(GpuImageProcessor&& other) = default;
  GpuImageProcessor& operator=(const GpuImageProcessor& other) = delete;
  GpuImageProcessor& operator=(GpuImageProcessor&& other) = default;

  ~GpuImageProcessor();

  // Convert the input |rgba_input| texture to NV12.
  //
  // Args:
  //    |rgba_input|:
  //        The input 2D texture to be converted.
  //    |y_output|:
  //        The output 2D texture for Y plane.  The texture must be of format
  //        R8.
  //    |uv_output|:
  //        The output 2D texture for UV plane.  The texture must be of format
  //        GR88.  The pixel dimension must be
  //        (|y_output|.width / 2, |y_output|.height / 2).
  //
  // Returns:
  //    true if GL commands are successfully submitted; false otherwise.
  bool RGBAToNV12(const Texture2D& rgba_input,
                  const Texture2D& y_output,
                  const Texture2D& uv_output);

  // Convert the input |external_yuv_input| texture to NV12.
  //
  // Args:
  //    |external_yuv_input|:
  //        The input external texture to be converted.  The texture will be
  //        bound to the TEXTURE_EXTERNAL_OES target for sampling.
  //    |y_output|:
  //        The output 2D texture for Y plane.  The texture must be of format
  //        R8.
  //    |uv_output|:
  //        The output 2D texture for UV plane.  The texture must be of format
  //        GR8.  The pixel dimension must be
  //        (|y_output|.width / 2, |y_output|.height / 2).
  //
  // Returns:
  //    true if GL commands are successfully submitted; false otherwise.
  bool ExternalYUVToNV12(const Texture2D& external_yuv_input,
                         const Texture2D& y_output,
                         const Texture2D& uv_output);

  // Convert the input |external_yuv_input| texture to RGBA.
  //
  // Args:
  //    |external_yuv_input|:
  //        The input external texture to be converted.  The texture will be
  //        bound to the TEXTURE_EXTERNAL_OES target for sampling.
  //    |rgba_output|:
  //        The output 2D texture.  The texture should have RGBA internal
  //        format.
  //
  // Returns:
  //    true if GL commands are successfully submitted; false otherwise.
  bool ExternalYUVToRGBA(const Texture2D& external_yuv_input,
                         const Texture2D& rgba_output);

  // Convert the input NV12 |y_input| and |uv_input| textures to RGBA.
  //
  // Args:
  //    |y_input|:
  //        The input 2D texture for Y plane.  The texture must be of format
  //        R8.
  //    |uv_input|:
  //        The input 2D texture for UV plane.  The texture must be of format
  //        GR8.  The pixel dimension must be
  //        (|y_input|.width / 2, |y_input|.height / 2).
  //    |rgba_output|:
  //        The output 2D texture.  The texture should have RGBA internal
  //        format.
  //
  // Returns:
  //    true if GL commands are successfully submitted; false otherwise.
  bool NV12ToRGBA(const Texture2D& y_input,
                  const Texture2D& uv_input,
                  const Texture2D& rgba_output);

  // Convert the input YUV |y_input| and |uv_input| textures to YUV with GPU
  // downsampling.  This can be used for conversion between NV12 and P010 pixel
  // formats.
  //
  // Args:
  //    |y_input|:
  //        The input 2D texture for Y plane.  The texture must be of format
  //        R8.
  //    |uv_input|:
  //        The input 2D texture for UV plane.  The texture must be of format
  //        GR8.  The pixel dimension must be
  //        (|y_input|.width / 2, |y_input|.height / 2).
  //    |y_output|:
  //        The output 2D texture for Y plane.  The texture must be of format
  //        R8.
  //    |uv_output|:
  //        The output 2D texture for UV plane.  The texture must be of format
  //        GR8.  The pixel dimension must be
  //        (|y_output|.width / 2, |y_output|.height / 2).
  //
  // Returns:
  //    true if GL commands are successfully submitted; false otherwise.
  bool YUVToYUV(const Texture2D& y_input,
                const Texture2D& uv_input,
                const Texture2D& y_output,
                const Texture2D& uv_output);

  // Apply the Gamma curve: OUT = pow(IN, 1/|gamma_value|) to each of the RGB
  // channels of |rgba_input|.  The results are written to |rgba_output|.
  //
  // Args:
  //    |gamma_value|:
  //        The Gamma parameter for the Gamma curve.
  //    |rgba_input|:
  //        The input RGBA texture to apply the Gamma curve to.
  //    |rgba_output|:
  //        The output RGBA texture to store the results.
  //
  // Returns:
  //    true if GL commands are successfully submitted; false otherwise.
  bool ApplyGammaCorrection(float gamma_value,
                            const Texture2D& rgba_input,
                            const Texture2D& rgba_output);

  // Take the RGB input from |rgba_input| and apply the lookup table |r_lut|,
  // |g_lut|, and |b_lut| to the R, G, B channels separately.  The result is
  // written to |rgba_output|.
  //
  // Args:
  //    |r_lut|:
  //        The (n x 1) lookup table for the R channel, where n is the number of
  //        points that approximates the LUT curve.
  //    |g_lut|:
  //        The (n x 1) lookup table for the G channel, where n is the number of
  //        points that approximates the LUT curve.
  //    |b_lut|:
  //        The (n x 1) lookup table for the B channel, where n is the number of
  //        points that approximates the LUT curve.
  //    |rgba_input|:
  //        The input RGBA texture to sample the RGB input to the RGB LUTs from.
  //    |rgba_output|:
  //        The output RGBA texture to store the results after the LUT
  //        operations.
  //
  // Returns:
  //    true if GL commands are successfully submitted; false otherwise.
  bool ApplyRgbLut(const Texture2D& r_lut,
                   const Texture2D& g_lut,
                   const Texture2D& b_lut,
                   const Texture2D& rgba_input,
                   const Texture2D& rgba_output);

 private:
  ScreenSpaceRect rect_;

  ShaderProgram rgba_to_nv12_program_;
  ShaderProgram external_yuv_to_nv12_program_;
  ShaderProgram external_yuv_to_rgba_program_;
  ShaderProgram nv12_to_rgba_program_;
  ShaderProgram nv12_to_nv12_program_;
  ShaderProgram gamma_correction_program_;
  ShaderProgram lut_program_;

  Sampler nearest_clamp_to_edge_;
  Sampler linear_clamp_to_edge_;
};

}  // namespace cros

#endif  // CAMERA_GPU_IMAGE_PROCESSOR_H_
