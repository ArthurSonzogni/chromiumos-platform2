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

namespace {

#define MACRO(ret_ty, fct, ...) DECLARE_HANDLE(fct)
#include "egl_loader_functions.def"  // NOLINT
#undef MACRO
DECLARE_HANDLE(eglGetProcAddress);

class LoadLibrary {
 public:
  LoadLibrary() {
    m_egl_lib = std::make_unique<GlLibraryWrapper>(
        cros::AngleEnabled() ? "/usr/lib64/angle/libEGL.so"
                             : "/usr/lib64/libEGL.so.1");

#define MACRO(ret_ty, fct, ...) LOAD_SYMBOL(m_egl_lib, fct)
#include "egl_loader_functions.def"  // NOLINT
#undef MACRO

    LOAD_SYMBOL(m_egl_lib, eglGetProcAddress);
  }

 private:
  std::unique_ptr<GlLibraryWrapper> m_egl_lib;
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
