// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RGBKBD_INTERNAL_RGB_KEYBOARD_H_
#define RGBKBD_INTERNAL_RGB_KEYBOARD_H_

#include <stdint.h>

#include "rgbkbd/rgb_keyboard.h"

namespace rgbkbd {

class InternalRgbKeyboard : public RgbKeyboard {
 public:
  InternalRgbKeyboard() = default;
  InternalRgbKeyboard(const InternalRgbKeyboard&) = delete;
  InternalRgbKeyboard& operator=(const InternalRgbKeyboard&) = delete;
  ~InternalRgbKeyboard() override = default;

  bool SetKeyColor(uint32_t key, uint8_t r, uint8_t g, uint8_t b) override;
  bool SetAllKeyColors(uint8_t r, uint8_t g, uint8_t b) override;
  bool GetRgbKeyboardCapabilities() override;
};

}  // namespace rgbkbd

#endif  // RGBKBD_INTERNAL_RGB_KEYBOARD_H_
