// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rgbkbd/rgb_keyboard_controller_impl.h"

namespace rgbkbd {
RgbKeyboardControllerImpl::RgbKeyboardControllerImpl() = default;
RgbKeyboardControllerImpl::~RgbKeyboardControllerImpl() = default;

uint32_t RgbKeyboardControllerImpl::GetRgbKeyboardCapabilities() {
  return static_cast<uint32_t>(keyboard_capabilities_);
}

}  // namespace rgbkbd
