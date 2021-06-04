// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREEN_CONTROLLER_INTERFACE_H_
#define MINIOS_SCREEN_CONTROLLER_INTERFACE_H_

#include "minios/screen_interface.h"
#include "minios/screen_types.h"

namespace minios {

class ScreenControllerInterface {
 public:
  ScreenControllerInterface() = default;
  virtual ~ScreenControllerInterface() = default;

  // Displays locale menu.
  virtual void SwitchLocale(ScreenInterface* screen) = 0;

  // Returns to previous screen and updates locale and related constants.
  virtual void UpdateLocale(ScreenInterface* screen, int locale_index) = 0;

  // Changes to the next action in flow and shows UI.
  virtual void OnForward(ScreenInterface* screen) = 0;

  // Changes to the previous action in flow and shows UI.
  virtual void OnBackward(ScreenInterface* screen) = 0;

  // Changes the screen to the given error.
  virtual void OnError(ScreenType error_screen) = 0;

  // Returns the current screen in flow.
  virtual ScreenType GetCurrentScreen() = 0;
};

}  // namespace minios

#endif  // MINIOS_SCREEN_CONTROLLER_INTERFACE_H_
