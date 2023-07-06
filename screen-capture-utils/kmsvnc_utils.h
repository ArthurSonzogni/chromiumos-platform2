// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SCREEN_CAPTURE_UTILS_KMSVNC_UTILS_H_
#define SCREEN_CAPTURE_UTILS_KMSVNC_UTILS_H_

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "screen-capture-utils/capture.h"

namespace screenshot {

// Gets the VNC width that's a multiple of 4. If |crtc_width|
// is not a multiple of 4, it'll be rounded up to the next
// multiple of 4. For instance, GetVncWidth(1366) => 1368.
uint32_t GetVncWidth(uint32_t crtc_width);

// Converts the display buffer |from| to an array of bytes |to|
// with |vnc_width| taken into consideration. VNC requires
// the display width to be a multiple of 4, thus |vnc_width|
// may be larger (by 1-3 pixels) than the actual display width.
void ConvertBuffer(const DisplayBuffer::Result& from,
                   uint32_t* to,
                   uint32_t vnc_width);

}  // namespace screenshot

#endif  // SCREEN_CAPTURE_UTILS_KMSVNC_UTILS_H_
