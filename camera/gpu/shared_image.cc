/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpu/shared_image.h"

#include <utility>

#include <drm_fourcc.h>
#include <hardware/gralloc.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/common.h"

namespace cros {

// static
SharedImage SharedImage::CreateFromBuffer(buffer_handle_t buffer,
                                          Texture2D::Target texture_target,
                                          bool separate_yuv_textures) {
  std::vector<EglImage> egl_images;
  std::vector<Texture2D> textures;
  if (!separate_yuv_textures) {
    egl_images.emplace_back(EglImage::FromBuffer(buffer));
    if (!egl_images[0].IsValid()) {
      LOGF(ERROR) << "Failed to create EGLimage for buffer";
      return SharedImage();
    }
    textures.emplace_back(texture_target, egl_images[0]);
    if (!textures[0].IsValid()) {
      LOGF(ERROR) << "Failed to bind EGLimage to texture";
      return SharedImage();
    }
  } else {
    uint32_t buffer_format = CameraBufferManager::GetDrmPixelFormat(buffer);
    if (buffer_format != DRM_FORMAT_NV12) {
      LOGF(ERROR) << "Only NV12 is supported";
      return SharedImage();
    }
    int buffer_width = static_cast<int>(CameraBufferManager::GetWidth(buffer));
    int buffer_height =
        static_cast<int>(CameraBufferManager::GetHeight(buffer));
    egl_images.emplace_back(EglImage::FromBufferPlane(
        buffer, 0, buffer_width, buffer_height, DRM_FORMAT_R8));
    if (!egl_images[0].IsValid()) {
      LOGF(ERROR) << "Failed to create EGLimage for Y plane";
      return SharedImage();
    }
    egl_images.emplace_back(EglImage::FromBufferPlane(
        buffer, 1, buffer_width / 2, buffer_height / 2, DRM_FORMAT_GR88));
    if (!egl_images[1].IsValid()) {
      LOGF(ERROR) << "Failed to create EGLimage for UV plane";
      return SharedImage();
    }
    textures.emplace_back(Texture2D::Target::kTarget2D, egl_images[0]);
    textures.emplace_back(Texture2D::Target::kTarget2D, egl_images[1]);
    if (!textures[0].IsValid() || !textures[1].IsValid()) {
      LOGF(ERROR) << "Failed to bind EGLimage to texture";
      return SharedImage();
    }
  }
  return SharedImage(buffer, std::move(egl_images), std::move(textures));
}

// static
SharedImage SharedImage::CreateFromGpuTexture(GLenum gl_format,
                                              int width,
                                              int height) {
  std::vector<Texture2D> textures;
  textures.emplace_back(gl_format, width, height);
  if (!textures[0].IsValid()) {
    LOGF(ERROR) << "Failed to create texture";
    return SharedImage();
  }
  return SharedImage(nullptr, std::vector<EglImage>(), std::move(textures));
}

SharedImage::SharedImage(buffer_handle_t buffer,
                         std::vector<EglImage> egl_images,
                         std::vector<Texture2D> textures)
    : buffer_(buffer),
      egl_images_(std::move(egl_images)),
      textures_(std::move(textures)) {
  CHECK(textures_[0].IsValid());
}

SharedImage::SharedImage(SharedImage&& other) {
  *this = std::move(other);
}

SharedImage& SharedImage::operator=(SharedImage&& other) {
  if (this != &other) {
    Invalidate();
    buffer_ = other.buffer_;
    egl_images_ = std::move(other.egl_images_);
    textures_ = std::move(other.textures_);
    destruction_callback_ = std::move(other.destruction_callback_);

    other.buffer_ = nullptr;
  }
  return *this;
}

SharedImage::~SharedImage() {
  Invalidate();
}

const Texture2D& SharedImage::texture() const {
  CHECK_EQ(textures_.size(), 1);
  return textures_[0];
}

const Texture2D& SharedImage::y_texture() const {
  CHECK_EQ(textures_.size(), 2);
  return textures_[0];
}

const Texture2D& SharedImage::uv_texture() const {
  CHECK_EQ(textures_.size(), 2);
  return textures_[1];
}

void SharedImage::SetDestructionCallback(base::OnceClosure callback) {
  destruction_callback_ = std::move(callback);
}

void SharedImage::Invalidate() {
  buffer_ = nullptr;
  egl_images_.clear();
  textures_.clear();
  if (!destruction_callback_.is_null()) {
    std::move(destruction_callback_).Run();
  }
}

}  // namespace cros
