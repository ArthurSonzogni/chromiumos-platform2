/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpu/image_processor.h"

#include <string>
#include <vector>

#include "cros-camera/common.h"
#include "gpu/embedded_gpu_shaders_toc.h"
#include "gpu/gles/framebuffer.h"
#include "gpu/gles/sampler.h"
#include "gpu/gles/shader.h"
#include "gpu/gles/state_guard.h"
#include "gpu/gles/transform.h"

namespace cros {

namespace {

constexpr const char* kVertexShaderFilename =
    "fullscreen_rect_highp_310_es.vert";
constexpr const char* kRgbaToNv12Filename = "rgba_to_nv12.frag";
constexpr const char* kExternalYuvToNv12Filename = "external_yuv_to_nv12.frag";
constexpr const char* kExternalYuvToRgbaFilename = "external_yuv_to_rgba.frag";
constexpr const char* kNv12ToRgbaFilename = "nv12_to_rgba.frag";
constexpr const char* kNv12ToNv12Filename = "nv12_to_nv12.frag";
constexpr const char* kGammaCorrectionFilename = "gamma_correction.frag";
constexpr const char* kLutFilename = "lut.frag";

}  // namespace

GpuImageProcessor::GpuImageProcessor()
    : nearest_clamp_to_edge_(NearestClampToEdge()),
      linear_clamp_to_edge_(LinearClampToEdge()) {
  EmbeddedFileToc gpu_shaders = GetEmbeddedGpuShadersToc();

  // Create the vextex shader.
  base::span<const char> src = gpu_shaders.Get(kVertexShaderFilename);
  Shader vertex_shader(GL_VERTEX_SHADER, std::string(src.data(), src.size()));
  CHECK(vertex_shader.IsValid());

  {
    base::span<const char> src = gpu_shaders.Get(kRgbaToNv12Filename);
    Shader fragment_shader(GL_FRAGMENT_SHADER,
                           std::string(src.data(), src.size()));
    CHECK(fragment_shader.IsValid());
    rgba_to_nv12_program_ = ShaderProgram({&vertex_shader, &fragment_shader});
  }
  {
    base::span<const char> src = gpu_shaders.Get(kExternalYuvToNv12Filename);
    Shader fragment_shader(GL_FRAGMENT_SHADER,
                           std::string(src.data(), src.size()));
    CHECK(fragment_shader.IsValid());
    external_yuv_to_nv12_program_ =
        ShaderProgram({&vertex_shader, &fragment_shader});
  }
  {
    base::span<const char> src = gpu_shaders.Get(kExternalYuvToRgbaFilename);
    Shader fragment_shader(GL_FRAGMENT_SHADER,
                           std::string(src.data(), src.size()));
    CHECK(fragment_shader.IsValid());
    external_yuv_to_rgba_program_ =
        ShaderProgram({&vertex_shader, &fragment_shader});
  }
  {
    base::span<const char> src = gpu_shaders.Get(kNv12ToRgbaFilename);
    Shader fragment_shader(GL_FRAGMENT_SHADER,
                           std::string(src.data(), src.size()));
    CHECK(fragment_shader.IsValid());
    nv12_to_rgba_program_ = ShaderProgram({&vertex_shader, &fragment_shader});
  }
  {
    base::span<const char> src = gpu_shaders.Get(kNv12ToNv12Filename);
    Shader fragment_shader(GL_FRAGMENT_SHADER,
                           std::string(src.data(), src.size()));
    CHECK(fragment_shader.IsValid());
    nv12_to_nv12_program_ = ShaderProgram({&vertex_shader, &fragment_shader});
  }
  {
    base::span<const char> src = gpu_shaders.Get(kGammaCorrectionFilename);
    Shader fragment_shader(GL_FRAGMENT_SHADER,
                           std::string(src.data(), src.size()));
    CHECK(fragment_shader.IsValid());
    gamma_correction_program_ =
        ShaderProgram({&vertex_shader, &fragment_shader});
  }
  {
    base::span<const char> src = gpu_shaders.Get(kLutFilename);
    Shader fragment_shader(GL_FRAGMENT_SHADER,
                           std::string(src.data(), src.size()));
    CHECK(fragment_shader.IsValid());
    lut_program_ = ShaderProgram({&vertex_shader, &fragment_shader});
  }
}

GpuImageProcessor::~GpuImageProcessor() = default;

bool GpuImageProcessor::RGBAToNV12(const Texture2D& rgba_input,
                                   const Texture2D& y_output,
                                   const Texture2D& uv_output) {
  if ((y_output.width() / 2 != uv_output.width()) ||
      (y_output.height() / 2 != uv_output.height())) {
    LOGF(ERROR) << "Invalid Y (" << y_output.width() << ", "
                << y_output.height() << ") and UV (" << uv_output.width()
                << ", " << uv_output.height() << ") output dimension";
    return false;
  }

  FramebufferGuard fb_guard;
  ViewportGuard viewport_guard;
  ProgramGuard program_guard;
  VertexArrayGuard va_guard;

  rect_.SetAsVertexInput();

  // Set up RGBA input texture.
  constexpr int kInputBinding = 0;
  glActiveTexture(GL_TEXTURE0 + kInputBinding);
  rgba_input.Bind();
  nearest_clamp_to_edge_.Bind(kInputBinding);

  rgba_to_nv12_program_.UseProgram();

  // Set shader uniforms.
  std::vector<float> texture_matrix = TextureSpaceFromNdc();
  GLint uTextureMatrix =
      rgba_to_nv12_program_.GetUniformLocation("uTextureMatrix");
  glUniformMatrix4fv(uTextureMatrix, 1, false, texture_matrix.data());
  GLint uIsYPlane = rgba_to_nv12_program_.GetUniformLocation("uIsYPlane");

  // Y pass.
  {
    glUniform1i(uIsYPlane, true);
    Framebuffer fb;
    fb.Bind();
    fb.Attach(GL_COLOR_ATTACHMENT0, y_output);
    glViewport(0, 0, y_output.width(), y_output.height());
    rect_.Draw();
  }

  // UV pass.
  {
    glUniform1i(uIsYPlane, false);
    Framebuffer fb;
    fb.Bind();
    fb.Attach(GL_COLOR_ATTACHMENT0, uv_output);
    glViewport(0, 0, uv_output.width(), uv_output.height());
    rect_.Draw();
  }

  // Clean up.
  glActiveTexture(GL_TEXTURE0 + kInputBinding);
  rgba_input.Unbind();
  Sampler::Unbind(kInputBinding);

  return true;
}

bool GpuImageProcessor::ExternalYUVToNV12(const Texture2D& external_yuv_input,
                                          const Texture2D& y_output,
                                          const Texture2D& uv_output) {
  if ((y_output.width() / 2 != uv_output.width()) ||
      (y_output.height() / 2 != uv_output.height())) {
    LOGF(ERROR) << "Invalid Y (" << y_output.width() << ", "
                << y_output.height() << ") and UV (" << uv_output.width()
                << ", " << uv_output.height() << ") output dimension";
    return false;
  }

  FramebufferGuard fb_guard;
  ViewportGuard viewport_guard;
  ProgramGuard program_guard;
  VertexArrayGuard va_guard;

  rect_.SetAsVertexInput();

  // Set up input external YUV texture.
  constexpr int kInputBinding = 0;
  glActiveTexture(GL_TEXTURE0 + kInputBinding);
  external_yuv_input.Bind();
  nearest_clamp_to_edge_.Bind(kInputBinding);

  external_yuv_to_nv12_program_.UseProgram();

  // Set shader uniforms.
  std::vector<float> texture_matrix = TextureSpaceFromNdc();
  GLint uTextureMatrix =
      rgba_to_nv12_program_.GetUniformLocation("uTextureMatrix");
  glUniformMatrix4fv(uTextureMatrix, 1, false, texture_matrix.data());
  GLint uIsYPlane = rgba_to_nv12_program_.GetUniformLocation("uIsYPlane");

  // Y pass.
  {
    glUniform1i(uIsYPlane, true);
    Framebuffer fb;
    fb.Bind();
    fb.Attach(GL_COLOR_ATTACHMENT0, y_output);
    glViewport(0, 0, y_output.width(), y_output.height());
    rect_.Draw();
  }

  // UV pass.
  {
    glUniform1i(uIsYPlane, false);
    Framebuffer fb;
    fb.Bind();
    fb.Attach(GL_COLOR_ATTACHMENT0, uv_output);
    glViewport(0, 0, uv_output.width(), uv_output.height());
    rect_.Draw();
  }

  // Clean up.
  glActiveTexture(GL_TEXTURE0 + kInputBinding);
  external_yuv_input.Unbind();
  Sampler::Unbind(kInputBinding);

  return true;
}

bool GpuImageProcessor::ExternalYUVToRGBA(const Texture2D& external_yuv_input,
                                          const Texture2D& rgba_output) {
  FramebufferGuard fb_guard;
  ViewportGuard viewport_guard;
  ProgramGuard program_guard;
  VertexArrayGuard va_guard;

  rect_.SetAsVertexInput();

  // Set up input external YUV texture.
  constexpr int kInputBinding = 0;
  glActiveTexture(GL_TEXTURE0 + kInputBinding);
  external_yuv_input.Bind();
  nearest_clamp_to_edge_.Bind(kInputBinding);

  external_yuv_to_rgba_program_.UseProgram();

  // Set shader uniforms.
  std::vector<float> texture_matrix = TextureSpaceFromNdc();
  GLint uTextureMatrix =
      external_yuv_to_rgba_program_.GetUniformLocation("uTextureMatrix");
  glUniformMatrix4fv(uTextureMatrix, 1, false, texture_matrix.data());

  Framebuffer fb;
  fb.Bind();
  fb.Attach(GL_COLOR_ATTACHMENT0, rgba_output);
  glViewport(0, 0, rgba_output.width(), rgba_output.height());
  rect_.Draw();

  // Clean up.
  glActiveTexture(GL_TEXTURE0 + kInputBinding);
  external_yuv_input.Unbind();
  Sampler::Unbind(kInputBinding);

  return true;
}

bool GpuImageProcessor::NV12ToRGBA(const Texture2D& y_input,
                                   const Texture2D& uv_input,
                                   const Texture2D& rgba_output) {
  if ((y_input.width() / 2 != uv_input.width()) ||
      (y_input.height() / 2 != uv_input.height())) {
    LOGF(ERROR) << "Invalid Y (" << y_input.width() << ", " << y_input.height()
                << ") and UV (" << uv_input.width() << ", " << uv_input.height()
                << ") input dimension";
    return false;
  }

  FramebufferGuard fb_guard;
  ViewportGuard viewport_guard;
  ProgramGuard program_guard;
  VertexArrayGuard va_guard;

  rect_.SetAsVertexInput();

  constexpr int kYInputBinding = 0;
  constexpr int kUvInputBinding = 1;
  glActiveTexture(GL_TEXTURE0 + kYInputBinding);
  y_input.Bind();
  nearest_clamp_to_edge_.Bind(kYInputBinding);
  glActiveTexture(GL_TEXTURE0 + kUvInputBinding);
  uv_input.Bind();
  nearest_clamp_to_edge_.Bind(kUvInputBinding);

  nv12_to_rgba_program_.UseProgram();

  // Set shader uniforms.
  std::vector<float> texture_matrix = TextureSpaceFromNdc();
  GLint uTextureMatrix =
      nv12_to_rgba_program_.GetUniformLocation("uTextureMatrix");
  glUniformMatrix4fv(uTextureMatrix, 1, false, texture_matrix.data());

  Framebuffer fb;
  fb.Bind();
  fb.Attach(GL_COLOR_ATTACHMENT0, rgba_output);
  glViewport(0, 0, rgba_output.width(), rgba_output.height());
  rect_.Draw();

  // Clean up.
  glActiveTexture(GL_TEXTURE0 + kYInputBinding);
  y_input.Unbind();
  Sampler::Unbind(kYInputBinding);
  glActiveTexture(GL_TEXTURE0 + kUvInputBinding);
  uv_input.Unbind();
  Sampler::Unbind(kUvInputBinding);

  return true;
}

bool GpuImageProcessor::NV12ToNV12(const Texture2D& y_input,
                                   const Texture2D& uv_input,
                                   const Texture2D& y_output,
                                   const Texture2D& uv_output) {
  if ((y_input.width() / 2 != uv_input.width()) ||
      (y_input.height() / 2 != uv_input.height())) {
    LOGF(ERROR) << "Invalid Y (" << y_input.width() << ", " << y_input.height()
                << ") and UV (" << uv_input.width() << ", " << uv_input.height()
                << ") input dimension";
    return false;
  }
  if ((y_output.width() / 2 != uv_output.width()) ||
      (y_output.height() / 2 != uv_output.height())) {
    LOGF(ERROR) << "Invalid Y (" << y_output.width() << ", "
                << y_output.height() << ") and UV (" << uv_output.width()
                << ", " << uv_output.height() << ") output dimension";
    return false;
  }

  FramebufferGuard fb_guard;
  ViewportGuard viewport_guard;
  ProgramGuard program_guard;
  VertexArrayGuard va_guard;

  rect_.SetAsVertexInput();

  constexpr int kYInputBinding = 0;
  constexpr int kUvInputBinding = 1;
  glActiveTexture(GL_TEXTURE0 + kYInputBinding);
  y_input.Bind();
  nearest_clamp_to_edge_.Bind(kYInputBinding);
  glActiveTexture(GL_TEXTURE0 + kUvInputBinding);
  uv_input.Bind();
  nearest_clamp_to_edge_.Bind(kUvInputBinding);

  nv12_to_nv12_program_.UseProgram();

  // Set shader uniforms.
  std::vector<float> texture_matrix = TextureSpaceFromNdc();
  GLint uTextureMatrix =
      nv12_to_nv12_program_.GetUniformLocation("uTextureMatrix");
  glUniformMatrix4fv(uTextureMatrix, 1, false, texture_matrix.data());
  GLint uIsYPlane = nv12_to_nv12_program_.GetUniformLocation("uIsYPlane");

  // Y pass.
  {
    glUniform1i(uIsYPlane, true);
    Framebuffer fb;
    fb.Bind();
    fb.Attach(GL_COLOR_ATTACHMENT0, y_output);
    glViewport(0, 0, y_output.width(), y_output.height());
    rect_.Draw();
  }

  // UV pass.
  {
    glUniform1i(uIsYPlane, false);
    Framebuffer fb;
    fb.Bind();
    fb.Attach(GL_COLOR_ATTACHMENT0, uv_output);
    glViewport(0, 0, uv_output.width(), uv_output.height());
    rect_.Draw();
  }

  // Clean up.
  glActiveTexture(GL_TEXTURE0 + kYInputBinding);
  y_input.Unbind();
  Sampler::Unbind(kYInputBinding);
  glActiveTexture(GL_TEXTURE0 + kUvInputBinding);
  uv_input.Unbind();
  Sampler::Unbind(kUvInputBinding);

  return true;
}

bool GpuImageProcessor::ApplyGammaCorrection(float gamma_value,
                                             const Texture2D& rgba_input,
                                             const Texture2D& rgba_output) {
  FramebufferGuard fb_guard;
  ViewportGuard viewport_guard;
  ProgramGuard program_guard;
  VertexArrayGuard va_guard;

  rect_.SetAsVertexInput();

  // Set up input RGBA texture.
  constexpr int kInputBinding = 0;
  glActiveTexture(GL_TEXTURE0 + kInputBinding);
  rgba_input.Bind();
  nearest_clamp_to_edge_.Bind(kInputBinding);

  gamma_correction_program_.UseProgram();

  // Set shader uniforms.
  std::vector<float> texture_matrix = TextureSpaceFromNdc();
  GLint uTextureMatrix =
      gamma_correction_program_.GetUniformLocation("uTextureMatrix");
  glUniformMatrix4fv(uTextureMatrix, 1, false, texture_matrix.data());
  GLint uGammaValue =
      gamma_correction_program_.GetUniformLocation("uGammaValue");
  glUniform1f(uGammaValue, gamma_value);

  Framebuffer fb;
  fb.Bind();
  fb.Attach(GL_COLOR_ATTACHMENT0, rgba_output);
  glViewport(0, 0, rgba_output.width(), rgba_output.height());
  rect_.Draw();

  // Clean up.
  glActiveTexture(GL_TEXTURE0 + kInputBinding);
  rgba_input.Unbind();
  Sampler::Unbind(kInputBinding);

  return true;
}

bool GpuImageProcessor::ApplyRgbLut(const Texture2D& r_lut,
                                    const Texture2D& g_lut,
                                    const Texture2D& b_lut,
                                    const Texture2D& rgba_input,
                                    const Texture2D& rgba_output) {
  if (!r_lut.IsValid() || !g_lut.IsValid() || !b_lut.IsValid()) {
    return false;
  }
  if (!rgba_input.IsValid() || !rgba_output.IsValid()) {
    return false;
  }

  FramebufferGuard fb_guard;
  ViewportGuard viewport_guard;
  ProgramGuard program_guard;
  VertexArrayGuard va_guard;

  rect_.SetAsVertexInput();

  constexpr int kInputBinding = 0;
  constexpr int kRLutBinding = 1;
  constexpr int kGLutBinding = 2;
  constexpr int kBLutBinding = 3;

  // Set up input RGBA texture.
  glActiveTexture(GL_TEXTURE0 + kInputBinding);
  rgba_input.Bind();
  nearest_clamp_to_edge_.Bind(kInputBinding);

  // Set up RGB LUT textures.
  glActiveTexture(GL_TEXTURE0 + kRLutBinding);
  r_lut.Bind();
  linear_clamp_to_edge_.Bind(kRLutBinding);
  glActiveTexture(GL_TEXTURE0 + kGLutBinding);
  g_lut.Bind();
  linear_clamp_to_edge_.Bind(kGLutBinding);
  glActiveTexture(GL_TEXTURE0 + kBLutBinding);
  b_lut.Bind();
  linear_clamp_to_edge_.Bind(kBLutBinding);

  lut_program_.UseProgram();

  // Set shader uniforms.
  std::vector<float> texture_matrix = TextureSpaceFromNdc();
  GLint uTextureMatrix = lut_program_.GetUniformLocation("uTextureMatrix");
  glUniformMatrix4fv(uTextureMatrix, 1, false, texture_matrix.data());

  Framebuffer fb;
  fb.Bind();
  fb.Attach(GL_COLOR_ATTACHMENT0, rgba_output);
  glViewport(0, 0, rgba_output.width(), rgba_output.height());
  rect_.Draw();

  // Clean up.
  glActiveTexture(GL_TEXTURE0 + kInputBinding);
  rgba_input.Unbind();
  Sampler::Unbind(kInputBinding);
  glActiveTexture(GL_TEXTURE0 + kRLutBinding);
  r_lut.Unbind();
  Sampler::Unbind(kRLutBinding);
  glActiveTexture(GL_TEXTURE0 + kGLutBinding);
  g_lut.Unbind();
  Sampler::Unbind(kGLutBinding);
  glActiveTexture(GL_TEXTURE0 + kBLutBinding);
  b_lut.Unbind();
  Sampler::Unbind(kBLutBinding);

  return true;
}

}  // namespace cros
