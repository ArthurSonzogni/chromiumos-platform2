/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/hdrnet_processor_device_adapter_ipu6.h"

#include <string>
#include <vector>

#include <base/strings/stringprintf.h>

#include "common/embed_file_toc.h"
#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/common.h"
#include "features/gcam_ae/ae_info.h"
#include "features/hdrnet/embedded_hdrnet_processor_shaders_ipu6_toc.h"
#include "features/hdrnet/ipu6_gamma.h"
#include "features/third_party/intel/intel_vendor_metadata_tags.h"
#include "gpu/embedded_gpu_shaders_toc.h"
#include "gpu/gles/framebuffer.h"
#include "gpu/gles/state_guard.h"
#include "gpu/gles/transform.h"

namespace cros {

namespace {

constexpr const char kVertexShaderFilename[] =
    "fullscreen_rect_highp_310_es.vert";
constexpr const char kPreprocessorFilename[] = "preprocess_ipu6.frag";
constexpr const char kPostprocessorFilename[] = "postprocess_ipu6.frag";

}  // namespace

HdrNetProcessorDeviceAdapterIpu6::HdrNetProcessorDeviceAdapterIpu6(
    const camera_metadata_t* static_info,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : task_runner_(task_runner) {
  base::Optional<int32_t> max_curve_points =
      GetRoMetadata<int32_t>(static_info, ANDROID_TONEMAP_MAX_CURVE_POINTS);
  CHECK(max_curve_points) << ": ANDROID_TONEMAP_MAX_CURVE_POINTS not set";
  num_curve_points_ = *max_curve_points;
}

bool HdrNetProcessorDeviceAdapterIpu6::Initialize() {
  DCHECK(task_runner_->BelongsToCurrentThread());

  rect_ = std::make_unique<ScreenSpaceRect>();
  nearest_clamp_to_edge_ = Sampler(NearestClampToEdge());
  linear_clamp_to_edge_ = Sampler(LinearClampToEdge());

  EmbeddedFileToc hdrnet_processor_shaders =
      GetEmbeddedHdrnetProcessorShadersIpu6Toc();
  EmbeddedFileToc gpu_shaders = GetEmbeddedGpuShadersToc();
  // Create the vextex shader.
  base::span<const char> src = gpu_shaders.Get(kVertexShaderFilename);
  Shader vertex_shader(GL_VERTEX_SHADER, std::string(src.data(), src.size()));
  if (!vertex_shader.IsValid()) {
    LOGF(ERROR) << "Failed to load vertex shader";
    return false;
  }

  {
    base::span<const char> src =
        hdrnet_processor_shaders.Get(kPreprocessorFilename);
    Shader fragment_shader(GL_FRAGMENT_SHADER,
                           std::string(src.data(), src.size()));
    if (!fragment_shader.IsValid()) {
      LOGF(ERROR) << "Failed to load preprocess shader";
      return false;
    }
    preprocessor_program_ = ShaderProgram({&vertex_shader, &fragment_shader});
  }
  {
    base::span<const char> src =
        hdrnet_processor_shaders.Get(kPostprocessorFilename);
    Shader fragment_shader(GL_FRAGMENT_SHADER,
                           std::string(src.data(), src.size()));
    if (!fragment_shader.IsValid()) {
      LOGF(ERROR) << "Failed to load postprocess shader";
      return false;
    }
    postprocessor_program_ = ShaderProgram({&vertex_shader, &fragment_shader});
  }

  gamma_lut_ = intel_ipu6::CreateGammaLutTexture();
  inverse_gamma_lut_ = intel_ipu6::CreateInverseGammaLutTexture();

  VLOGF(1) << "Created IPU6 HDRnet device processor";
  initialized_ = true;
  return true;
}

void HdrNetProcessorDeviceAdapterIpu6::TearDown() {
  DCHECK(task_runner_->BelongsToCurrentThread());
}

bool HdrNetProcessorDeviceAdapterIpu6::WriteRequestParameters(
    Camera3CaptureDescriptor* request, MetadataLogger* metadata_logger) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  std::array<uint8_t, 1> tonemap_curve_enable = {
      INTEL_VENDOR_CAMERA_CALLBACK_TM_CURVE_TRUE};
  if (!request->UpdateMetadata<uint8_t>(INTEL_VENDOR_CAMERA_CALLBACK_TM_CURVE,
                                        tonemap_curve_enable)) {
    LOGF(ERROR) << "Cannot enable INTEL_VENDOR_CAMERA_CALLBACK_TM_CURVE in "
                   "request metadta";
    return false;
  }
  return true;
}

void HdrNetProcessorDeviceAdapterIpu6::ProcessResultMetadata(
    Camera3CaptureDescriptor* result, MetadataLogger* metadata_logger) {
  DCHECK(task_runner_->BelongsToCurrentThread());
  // TODO(jcliang): Theoretically metadata can come after the buffer as well.
  // Currently the pipeline would break if the metadata come after the buffers.
  if (!initialized_) {
    LOGF(ERROR) << "HDRnet processor hadn't been initialized";
    return;
  }

  base::span<const float> tonemap_curve =
      result->GetMetadata<float>(INTEL_VENDOR_CAMERA_TONE_MAP_CURVE);
  if (!tonemap_curve.empty()) {
    VLOGF(1) << "Update GTM curve";
    CHECK_EQ(tonemap_curve.size(), num_curve_points_ * 2);
    gtm_lut_ = CreateGainLutTexture(tonemap_curve, false);
    inverse_gtm_lut_ = CreateGainLutTexture(tonemap_curve, true);

    if (metadata_logger) {
      metadata_logger->Log(result->frame_number(), kTagToneMapCurve,
                           tonemap_curve);
    }
  }
}

bool HdrNetProcessorDeviceAdapterIpu6::Preprocess(
    const HdrNetConfig::Options& options,
    const SharedImage& input_yuv,
    const SharedImage& output_rgba) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  if (!inverse_gtm_lut_.IsValid()) {
    LOGF(ERROR) << "Invalid GTM curve textures";
    return false;
  }
  // Intel's GLES implementation always samples the YUV image with narrow range
  // color space and it's crushing the shadow areas on the images. Before we
  // have a fix in mesa, sample and covert the YUV image to RGB ourselves.
  if (!input_yuv.y_texture().IsValid() || !input_yuv.uv_texture().IsValid() ||
      !output_rgba.texture().IsValid()) {
    LOGF(ERROR) << "Invalid input or output textures";
    return false;
  }
  if ((input_yuv.y_texture().width() / 2 != input_yuv.uv_texture().width()) ||
      (input_yuv.y_texture().height() / 2 != input_yuv.uv_texture().height())) {
    LOG(ERROR) << "Invalid Y (" << input_yuv.y_texture().width() << ", "
               << input_yuv.y_texture().height() << ") and UV ("
               << input_yuv.uv_texture().width() << ", "
               << input_yuv.uv_texture().height() << ") output dimension";
    return false;
  }

  FramebufferGuard fb_guard;
  ViewportGuard viewport_guard;
  ProgramGuard program_guard;
  VertexArrayGuard va_guard;

  rect_->SetAsVertexInput();

  constexpr int kYInputBinding = 0;
  constexpr int kUvInputBinding = 1;
  constexpr int kInverseGammaLutBinding = 2;
  constexpr int kInverseGtmLutBinding = 3;

  glActiveTexture(GL_TEXTURE0 + kYInputBinding);
  input_yuv.y_texture().Bind();
  nearest_clamp_to_edge_.Bind(kYInputBinding);
  glActiveTexture(GL_TEXTURE0 + kUvInputBinding);
  input_yuv.uv_texture().Bind();
  nearest_clamp_to_edge_.Bind(kUvInputBinding);
  glActiveTexture(GL_TEXTURE0 + kInverseGammaLutBinding);
  inverse_gamma_lut_.Bind();
  linear_clamp_to_edge_.Bind(kInverseGammaLutBinding);
  glActiveTexture(GL_TEXTURE0 + kInverseGtmLutBinding);
  inverse_gtm_lut_.Bind();
  linear_clamp_to_edge_.Bind(kInverseGtmLutBinding);

  preprocessor_program_.UseProgram();

  // Set shader uniforms.
  std::vector<float> texture_matrix = TextureSpaceFromNdc();
  GLint uTextureMatrix =
      preprocessor_program_.GetUniformLocation("uTextureMatrix");
  glUniformMatrix4fv(uTextureMatrix, 1, false, texture_matrix.data());

  Framebuffer fb;
  fb.Bind();
  glViewport(0, 0, output_rgba.texture().width(),
             output_rgba.texture().height());
  fb.Attach(GL_COLOR_ATTACHMENT0, output_rgba.texture());
  rect_->Draw();

  // Clean up.
  glActiveTexture(GL_TEXTURE0 + kYInputBinding);
  input_yuv.y_texture().Unbind();
  Sampler::Unbind(kYInputBinding);
  glActiveTexture(GL_TEXTURE0 + kUvInputBinding);
  input_yuv.uv_texture().Unbind();
  Sampler::Unbind(kUvInputBinding);
  glActiveTexture(GL_TEXTURE0 + kInverseGammaLutBinding);
  inverse_gamma_lut_.Unbind();
  Sampler::Unbind(kInverseGammaLutBinding);
  glActiveTexture(GL_TEXTURE0 + kInverseGtmLutBinding);
  inverse_gtm_lut_.Unbind();
  Sampler::Unbind(kInverseGtmLutBinding);

  return true;
}

bool HdrNetProcessorDeviceAdapterIpu6::Postprocess(
    const HdrNetConfig::Options& options,
    const SharedImage& input_rgba,
    const SharedImage& output_nv12) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  if (!gtm_lut_.IsValid()) {
    return false;
  }
  if (!input_rgba.texture().IsValid() || !output_nv12.y_texture().IsValid() ||
      !output_nv12.uv_texture().IsValid()) {
    return false;
  }
  if ((output_nv12.y_texture().width() / 2 !=
       output_nv12.uv_texture().width()) ||
      (output_nv12.y_texture().height() / 2 !=
       output_nv12.uv_texture().height())) {
    LOGF(ERROR) << "Invalid Y (" << output_nv12.y_texture().width() << ", "
                << output_nv12.y_texture().height() << ") and UV ("
                << output_nv12.uv_texture().width() << ", "
                << output_nv12.uv_texture().height() << ") output dimension";
    return false;
  }

  FramebufferGuard fb_guard;
  ViewportGuard viewport_guard;
  ProgramGuard program_guard;
  VertexArrayGuard va_guard;

  rect_->SetAsVertexInput();

  constexpr int kInputBinding = 0;
  constexpr int kGammaLutBinding = 1;
  constexpr int kGtmLutBinding = 2;

  glActiveTexture(GL_TEXTURE0 + kInputBinding);
  input_rgba.texture().Bind();
  nearest_clamp_to_edge_.Bind(kInputBinding);
  glActiveTexture(GL_TEXTURE0 + kGammaLutBinding);
  gamma_lut_.Bind();
  linear_clamp_to_edge_.Bind(kGammaLutBinding);
  glActiveTexture(GL_TEXTURE0 + kGtmLutBinding);
  gtm_lut_.Bind();
  linear_clamp_to_edge_.Bind(kGtmLutBinding);

  postprocessor_program_.UseProgram();

  // Set shader uniforms.
  std::vector<float> texture_matrix = TextureSpaceFromNdc();
  GLint uTextureMatrix =
      postprocessor_program_.GetUniformLocation("uTextureMatrix");
  glUniformMatrix4fv(uTextureMatrix, 1, false, texture_matrix.data());
  GLint uIsYPlane = postprocessor_program_.GetUniformLocation("uIsYPlane");

  Framebuffer fb;
  fb.Bind();
  // Y pass.
  {
    glUniform1i(uIsYPlane, true);
    glViewport(0, 0, output_nv12.y_texture().width(),
               output_nv12.y_texture().height());
    fb.Attach(GL_COLOR_ATTACHMENT0, output_nv12.y_texture());
    rect_->Draw();
  }
  // UV pass.
  {
    glUniform1i(uIsYPlane, false);
    glViewport(0, 0, output_nv12.uv_texture().width(),
               output_nv12.uv_texture().height());
    fb.Attach(GL_COLOR_ATTACHMENT0, output_nv12.uv_texture());
    rect_->Draw();
  }

  // Clean up.
  glActiveTexture(GL_TEXTURE0 + kInputBinding);
  input_rgba.texture().Unbind();
  Sampler::Unbind(kInputBinding);
  glActiveTexture(GL_TEXTURE0 + kGammaLutBinding);
  gamma_lut_.Unbind();
  Sampler::Unbind(kGammaLutBinding);
  glActiveTexture(GL_TEXTURE0 + kGtmLutBinding);
  gtm_lut_.Unbind();
  Sampler::Unbind(kGtmLutBinding);

  return true;
}

Texture2D HdrNetProcessorDeviceAdapterIpu6::CreateGainLutTexture(
    base::span<const float> tonemap_curve, bool inverse) {
  auto interpolate = [](float i, float x0, float y0, float x1,
                        float y1) -> float {
    float kEpsilon = 1e-8;
    if (std::abs(x1 - x0) < kEpsilon) {
      return y0;
    }
    float slope = (y1 - y0) / (x1 - x0);
    return y0 + (i - x0) * slope;
  };

  if (gtm_lut_buffer_.size() < num_curve_points_) {
    gtm_lut_buffer_.resize(num_curve_points_);
  }

  // |tonemap_curve| is an array of |num_curve_points_| (v, g) pairs of floats,
  // with v in [0, 1] and g > 0. Each (v, g) pair specifies the gain `g` to
  // apply when the pixel value is `v`. Note that the Intel IPU6 GTM LUT is
  // "gain-based" and is different from the plain LUT as defined in [1]. It is
  // assumed that v * g is non-decreasing otherwise the LUT cannot be reasonably
  // inversed.
  //
  // For the forward LUT, we build a table with |num_curve_points_| (v, g)
  // entries, where `g` is the gain to apply for pre-gain pixel value `v`. This
  // is similar to the input |tonemap_curve|.
  //
  // For the inverse LUT, we build a table with |num_curve_points_| (u, g)
  // entries, where `g` is the estimated gain applied on post-gain pixel value
  // `u`. The shader would divide `u` by `g` to transform the pixel value back
  // to pseudo-linear domain.
  //
  // [1]:
  // https://developer.android.com/reference/android/hardware/camera2/CaptureRequest#TONEMAP_CURVE
  int lut_index = 0;
  float x0 = 0.0, y0 = 1.0;
  for (int i = 0; i < num_curve_points_; ++i) {
    int idx = i * 2;
    float x1 = tonemap_curve[idx], y1 = tonemap_curve[idx + 1];
    if (inverse) {
      x1 = x1 * y1;  // x-axis is the value with gain applied.
    }
    const int scaled_x1 = x1 * num_curve_points_;
    for (; lut_index <= scaled_x1 && lut_index < num_curve_points_;
         ++lut_index) {
      gtm_lut_buffer_[lut_index] = interpolate(
          static_cast<float>(lut_index) / num_curve_points_, x0, y0, x1, y1);
      VLOGF(1) << base::StringPrintf("(%5d, %1.10f, %d)", lut_index,
                                     gtm_lut_buffer_[lut_index], inverse);
    }
    x0 = x1;
    y0 = y1;
  }
  for (; lut_index < num_curve_points_; ++lut_index) {
    gtm_lut_buffer_[lut_index] = interpolate(
        static_cast<float>(lut_index) / num_curve_points_, x0, y0, 1.0, 1.0);
    VLOGF(1) << base::StringPrintf("(%5d, %1.10f, %d)", lut_index,
                                   gtm_lut_buffer_[lut_index], inverse);
  }

  Texture2D lut_texture(GL_R16F, num_curve_points_, 1);
  CHECK(lut_texture.IsValid());
  lut_texture.Bind();
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, num_curve_points_, 1, GL_RED,
                  GL_FLOAT, gtm_lut_buffer_.data());
  return lut_texture;
}

}  // namespace cros
