/*
 * Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <GLES3/gl3.h>

#include "cros-camera/angle_state.h"
#include "gpu/gl_loader/gl_loader.h"

namespace {

#define MACRO(ret_ty, fct, ...) DECLARE_HANDLE(fct)
#include "gles_loader_functions.def"  // NOLINT
#undef MACRO

class LoadLibrary {
 public:
  LoadLibrary() {
    m_gles_lib = std::make_unique<GlLibraryWrapper>(
        cros::AngleEnabled() ? "/usr/lib64/angle/libGLESv2.so"
                             : "/usr/lib64/libGLESv2.so.2");

#define MACRO(ret_ty, fct, ...) LOAD_SYMBOL(m_gles_lib, fct)
#include "gles_loader_functions.def"  // NOLINT
#undef MACRO
  }

 private:
  std::unique_ptr<GlLibraryWrapper> m_gles_lib;
};
LoadLibrary lib;

}  // namespace

#define MACRO(ret_ty, fct, ...) DECLARE_FUNCTION(ret_ty, fct, __VA_ARGS__)
#include "gles_loader_functions.def"  // NOLINT
#undef MACRO
