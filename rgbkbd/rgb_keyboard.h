// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RGBKBD_RGB_KEYBOARD_H_
#define RGBKBD_RGB_KEYBOARD_H_

namespace rgbkbd {

// Base interface class that exposes the API to interact with the keyboard's RGB
// service.
class RgbKeyboard {
 public:
  RgbKeyboard() = default;
  virtual ~RgbKeyboard() = default;

  virtual bool SetKeyColor(uint32_t key, uint8_t r, uint8_t g, uint8_t b) = 0;
  virtual bool SetAllKeyColors(uint8_t r, uint8_t g, uint8_t b) = 0;
  virtual bool GetRgbKeyboardCapabilities() = 0;
};

}  // namespace rgbkbd

#endif  // RGBKBD_RGB_KEYBOARD_H_
