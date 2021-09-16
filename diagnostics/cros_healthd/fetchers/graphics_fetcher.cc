// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/graphics_fetcher.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl32.h>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/strings/string_split.h>

#include "diagnostics/cros_healthd/utils/error_utils.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

}  // namespace

mojo_ipc::GraphicsResultPtr GraphicsFetcher::FetchGraphicsInfo(
    std::unique_ptr<EglManager> egl_manager) {
  auto graphics_info = mojo_ipc::GraphicsInfo::New();

  auto& gles_info = graphics_info->gles_info;
  auto error = FetchGraphicsInfo(std::move(egl_manager), &gles_info);
  if (error.has_value()) {
    return mojo_ipc::GraphicsResult::NewError(std::move(error.value()));
  }

  return mojo_ipc::GraphicsResult::NewGraphicsInfo(std::move(graphics_info));
}

std::unique_ptr<EglManager> EglManager::Create() {
  std::unique_ptr<EglManager> egl_manager(new EglManager());
  // CloudReady(CR) uses mesa-reven package for graphics driver, and the
  // graphics stack in CR is more complicated than usual ChromeOS devices. So in
  // CR, we need to use EGL v1.5 API to fetch the graphics info. However, not
  // all boards support v1.5 API(*1). So we need this USE flag in build time to
  // distinguish the case.
  //
  // (*1): For example, Asurada uses mali driver, and they don't support v1.5
  // EGL API at this moment. Asking them to upgrade their driver needs a long
  // time to go. Hence, we decide to use an USE flag to unblock this case.
#if defined(USE_MESA_REVEN)
  egl_manager->egl_display_ = eglGetPlatformDisplay(
      EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
#else
  egl_manager->egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif
  if (eglInitialize(egl_manager->egl_display_, /*major=*/nullptr,
                    /*minor=*/nullptr) != EGL_TRUE) {
    return nullptr;
  }

  eglBindAPI(EGL_OPENGL_ES_API);

  std::vector<EGLint> context_attribs = {
      EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE,
  };
  egl_manager->egl_context_ =
      eglCreateContext(egl_manager->egl_display_, EGL_NO_CONFIG_KHR,
                       EGL_NO_CONTEXT, context_attribs.data());
  if (egl_manager->egl_context_ == EGL_NO_CONTEXT) {
    return nullptr;
  }

  if (eglMakeCurrent(egl_manager->egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                     egl_manager->egl_context_) != EGL_TRUE) {
    return nullptr;
  }

  return egl_manager;
}

EglManager::~EglManager() {
  eglReleaseThread();
  eglDestroyContext(egl_display_, egl_context_);
}

mojo_ipc::GLESInfoPtr EglManager::FetchGLESInfo() {
  auto gles_info = mojo_ipc::GLESInfo::New();
  gles_info->version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
  gles_info->shading_version =
      reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
  gles_info->vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
  gles_info->renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  std::string gles_extensions(
      reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)));
  gles_info->extensions = base::SplitString(
      gles_extensions, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  return gles_info;
}

base::Optional<mojo_ipc::ProbeErrorPtr> GraphicsFetcher::FetchGraphicsInfo(
    std::unique_ptr<EglManager> egl_manager,
    mojo_ipc::GLESInfoPtr* out_gles_info) {
  if (!egl_manager) {
    egl_manager = EglManager::Create();
  }
  if (!egl_manager) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kSystemUtilityError,
                                  "Failed to initialze EglManager.");
  }

  auto gles_info = egl_manager->FetchGLESInfo();
  *out_gles_info = std::move(gles_info);

  return base::nullopt;
}

}  // namespace diagnostics
