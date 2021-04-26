/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_GPU_SHARED_IMAGE_H_
#define CAMERA_GPU_SHARED_IMAGE_H_

#include <vector>

#include <cutils/native_handle.h>

#include "gpu/egl/egl_context.h"
#include "gpu/gles/texture_2d.h"

namespace cros {

// SharedImage holds the different "handles" of a buffer object and is used to
// shared the same buffer across different components (mainly between CPU and
// GPU) without needing to explicitly copying the buffer content.
class SharedImage {
 public:
  // Creates a SharedImage from the give buffer handle |buffer|. |buffer| will
  // be bound to the texture target |texture_target| if |separate_yuv_textures|
  // is false. If |separate_yuv_textures| is true, then |buffer| will be bound
  // to the Texture2D texture target, since TextureExternalOES doesn't work if
  // we need to write to the underlying DMA-buf.
  static SharedImage CreateFromBuffer(buffer_handle_t buffer,
                                      Texture2D::Target texture_target,
                                      bool separate_yuv_textures = false);

  // Creates a SharedImage with the given GL format |gl_format| and dimension
  // |width| x |height|. The SharedImage image is a pure container of some GPU
  // textures and no DMA-buf buffer will be associated.
  static SharedImage CreateFromGpuTexture(GLenum gl_format,
                                          int width,
                                          int height);

  // Default constructor creates an invalid SharedImage.
  SharedImage() = default;

  SharedImage(const SharedImage& other) = delete;
  SharedImage(SharedImage&&);
  SharedImage& operator=(const SharedImage& other) = delete;
  SharedImage& operator=(SharedImage&& other);
  ~SharedImage();

  const buffer_handle_t& buffer() const { return buffer_; }
  const Texture2D& texture() const;
  const Texture2D& y_texture() const;
  const Texture2D& uv_texture() const;

  void SetDestructionCallback(base::OnceClosure callback);

 private:
  // Creates a SharedImage from the given |buffer|, |egl_images| and |textures|.
  // |buffer| and |egl_images| can be invalid, in which case the SharedImage is
  // simply a container for |textures|.
  //
  // Does not take ownership of |buffer|. The caller must make sure that
  // |buffer| out-lives the SharedImage it's bound to.
  //
  // Takes ownership of |egl_images| and |textures|.
  SharedImage(buffer_handle_t buffer,
              std::vector<EglImage> egl_images,
              std::vector<Texture2D> textures);

  void Invalidate();

  buffer_handle_t buffer_ = nullptr;
  std::vector<EglImage> egl_images_;
  std::vector<Texture2D> textures_;
  base::OnceClosure destruction_callback_;
};

}  // namespace cros

#endif  // CAMERA_GPU_SHARED_IMAGE_H_
