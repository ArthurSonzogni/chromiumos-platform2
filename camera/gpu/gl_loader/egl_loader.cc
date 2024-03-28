/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglplatform.h>

#include "cros-camera/angle_state.h"
#include "gpu/gl_loader/gl_loader.h"

#include <vector>

namespace {

#define MACRO(ret_ty, fct, ...) DECLARE_HANDLE(fct)
#include "egl_loader_functions.def"  // NOLINT
#undef MACRO
DECLARE_HANDLE(eglGetProcAddress);

class LoadLibrary {
 public:
  LoadLibrary() {
    m_use_angle = cros::AngleEnabled();
    m_egl_lib = std::make_unique<GlLibraryWrapper>(
        m_use_angle ? "/usr/lib64/angle/libEGL.so" : "/usr/lib64/libEGL.so.1");

#define MACRO(ret_ty, fct, ...) LOAD_SYMBOL(m_egl_lib, fct)
#include "egl_loader_functions.def"  // NOLINT
#undef MACRO

    LOAD_SYMBOL(m_egl_lib, eglGetProcAddress);
  }
  bool use_angle() { return m_use_angle; }

 private:
  std::unique_ptr<GlLibraryWrapper> m_egl_lib;
  bool m_use_angle;
};
LoadLibrary lib;

}  // namespace

#define MACRO(ret_ty, fct, ...) DECLARE_FUNCTION(ret_ty, fct, __VA_ARGS__)
#include "egl_loader_functions.def"  // NOLINT
#undef MACRO

__attribute__((__visibility__("default"))) extern "C" void(  // NOLINT
    *eglGetProcAddress(char const* procname))(void) {
  return reinterpret_cast<PFNEGLGETPROCADDRESSPROC>(_eglGetProcAddress)(
      procname);
}
