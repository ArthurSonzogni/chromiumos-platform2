// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rgbkbd/internal_rgb_keyboard.h"

#include <stdint.h>

#include <base/notreached.h>

namespace rgbkbd {

bool InternalRgbKeyboard::SetKeyColor(uint32_t key,
                                      uint8_t r,
                                      uint8_t g,
                                      uint8_t b) {
  NOTIMPLEMENTED();
  return true;
}

bool InternalRgbKeyboard::SetAllKeyColors(uint8_t r, uint8_t g, uint8_t b) {
  NOTIMPLEMENTED();
  return true;
}

}  // namespace rgbkbd
