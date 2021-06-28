/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpu/egl/egl_context.h"

#include <utility>
#include <vector>

#include "cros-camera/common.h"
#include "gpu/egl/utils.h"

namespace cros {

// static
std::unique_ptr<EglContext> EglContext::GetSurfacelessContext() {
  EGLDisplay egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (eglInitialize(egl_display, /*major=*/nullptr, /*minor=*/nullptr) !=
      EGL_TRUE) {
    LOG(FATAL) << "Failed to create EGL display";
  }
  // This will leak |egl_display|, but it should be okay.
  return std::make_unique<EglContext>(egl_display);
}

EglContext::EglContext(EGLDisplay display) : display_(display) {
  // Bind API.
  eglBindAPI(EGL_OPENGL_ES_API);

  EGLConfig config = EGL_NO_CONFIG_KHR;
  std::vector<EGLint> context_attribs = {
      EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE,
  };
  EGLContext share_context = EGL_NO_CONTEXT;
  context_ =
      eglCreateContext(display_, config, share_context, context_attribs.data());
}

EglContext::EglContext(EglContext&& other) {
  *this = std::move(other);
}

EglContext& EglContext::operator=(EglContext&& other) {
  if (this != &other) {
    Invalidate();
    display_ = other.display_;
    context_ = other.context_;

    other.display_ = EGL_NO_DISPLAY;
    other.context_ = EGL_NO_CONTEXT;
  }
  return *this;
}

EglContext::~EglContext() {
  Invalidate();
}

bool EglContext::IsCurrent() const {
  if (!IsValid()) {
    return false;
  }
  return context_ == eglGetCurrentContext();
}

bool EglContext::MakeCurrent() {
  if (!IsValid()) {
    LOGF(ERROR) << "Cannot make invalid context current";
    return false;
  }
  EGLSurface draw_surface = EGL_NO_SURFACE;
  EGLSurface read_surface = EGL_NO_SURFACE;
  EGLBoolean ok =
      eglMakeCurrent(display_, draw_surface, read_surface, context_);
  EGLint error = eglGetError();
  if (error != EGL_SUCCESS) {
    LOG(ERROR) << "Failed to make context current: "
               << EglGetErrorString(error);
  }
  return ok == EGL_TRUE;
}

void EglContext::Invalidate() {
  if (IsValid()) {
    if (IsCurrent()) {
      eglReleaseThread();
    }
    if (display_ != EGL_NO_DISPLAY) {
      eglDestroyContext(display_, context_);
      display_ = EGL_NO_DISPLAY;
    }
    context_ = EGL_NO_CONTEXT;
  }
}

}  // namespace cros
