/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_GPU_EGL_EGL_CONTEXT_H_
#define CAMERA_GPU_EGL_EGL_CONTEXT_H_

#include <memory>

#include <base/callback.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace cros {

// A RAII helper class that encapsulates an EGLContext object.
//
// TODO(jcliang): Allow configuring the context attributes on construction.
class EglContext {
 public:
  // Gets a surfaceless EGL context for offscreen rendering. This requires the
  // EGL_KHR_surfaceless_context extension, which should be supported on all
  // CrOS devices.
  static std::unique_ptr<EglContext> GetSurfacelessContext();

  // Creates and initializes an EGLContext. Does not take ownership of
  // |display|.
  explicit EglContext(EGLDisplay display);

  EglContext(const EglContext& other) = delete;
  EglContext(EglContext&& other);
  EglContext& operator=(const EglContext& other) = delete;
  EglContext& operator=(EglContext&&);
  ~EglContext();

  bool IsValid() const { return context_ != EGL_NO_CONTEXT; }

  // Checks if the EglContext is the current context.
  bool IsCurrent() const;

  // Makes the EglContext the current context.
  bool MakeCurrent();

 private:
  // Invalidates the EglContext instance.
  void Invalidate();

  EGLDisplay display_ = EGL_NO_DISPLAY;
  EGLContext context_ = EGL_NO_CONTEXT;
};

}  // namespace cros

#endif  // CAMERA_GPU_EGL_EGL_CONTEXT_H_
