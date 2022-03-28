// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RGBKBD_RGB_KEYBOARD_CONTROLLER_IMPL_H_
#define RGBKBD_RGB_KEYBOARD_CONTROLLER_IMPL_H_

#include <cstdint>
#include <dbus/rgbkbd/dbus-constants.h>

#include "rgbkbd/rgb_keyboard_controller.h"

namespace rgbkbd {

class RgbKeyboardControllerImpl : public RgbKeyboardController {
 public:
  // TODO(michaelcheco): Integrate `KeyboardbacklightLogger` class.
  RgbKeyboardControllerImpl();
  ~RgbKeyboardControllerImpl();
  RgbKeyboardControllerImpl(const RgbKeyboardControllerImpl&) = delete;
  RgbKeyboardControllerImpl& operator=(const RgbKeyboardControllerImpl&) =
      delete;

  uint32_t GetRgbKeyboardCapabilities() override;

 private:
  RgbKeyboardCapabilities keyboard_capabilities_ =
      RgbKeyboardCapabilities::kNone;
};

}  // namespace rgbkbd

#endif  // RGBKBD_RGB_KEYBOARD_CONTROLLER_IMPL_H_
