/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_GPU_TEST_SUPPORT_GL_TEST_FIXTURE_H_
#define CAMERA_GPU_TEST_SUPPORT_GL_TEST_FIXTURE_H_

#include <memory>

#pragma push_macro("None")
#pragma push_macro("Bool")
#undef None
#undef Bool

// gtest's internal typedef of None and Bool conflicts with the None and Bool
// macros in X11/X.h (https://github.com/google/googletest/issues/371).
// X11/X.h is pulled in by the GL headers we include.
#include <gtest/gtest.h>

#pragma pop_macro("None")
#pragma pop_macro("Bool")

#include <hardware/gralloc.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/common.h"
#include "gpu/egl/egl_context.h"
#include "gpu/egl/utils.h"
#include "gpu/gles/utils.h"

namespace cros {

class GlTestFixture : public ::testing::Test {
 protected:
  GlTestFixture();
  ~GlTestFixture() override = default;

  std::array<uint8_t, 4> GetTestRgbaColor(int x, int y, int width, int height);
  std::array<uint8_t, 3> GetTestYuvColor(int x, int y, int width, int height);
  void FillTestPattern(buffer_handle_t buffer);

  std::unique_ptr<EglContext> egl_context_;
};

}  // namespace cros

#endif  // CAMERA_GPU_TEST_SUPPORT_GL_TEST_FIXTURE_H_
