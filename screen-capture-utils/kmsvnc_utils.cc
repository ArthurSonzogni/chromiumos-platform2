// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check_op.h>
#include <cstdint>

#include "screen-capture-utils/egl_capture.h"
#include "screen-capture-utils/kmsvnc_utils.h"

namespace screenshot {

uint32_t GetVncWidth(uint32_t crtc_width) {
  const uint32_t vnc_width = (crtc_width + 3) / 4 * 4;
  CHECK_LT(vnc_width - crtc_width, 4);
  CHECK_GE(vnc_width, crtc_width);
  return vnc_width;
}

void ConvertBuffer(const DisplayBuffer::Result& from,
                   uint32_t* to,
                   uint32_t vnc_width) {
  // For cases where vnc width != display width(vnc needs to be a multiple of 4)
  // then vnc width will always be greater than display width.
  // In that case, we are copying only the available pixels from the display
  // buffer, and leaving the remainder as-is (zero valued)
  for (int i = 0; i < from.height; i++) {
    memcpy(to + vnc_width * i,
           static_cast<char*>(from.buffer) + from.stride * i,
           from.width * kBytesPerPixel);
  }
}

}  // namespace screenshot
