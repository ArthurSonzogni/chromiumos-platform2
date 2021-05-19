// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREEN_INTERFACE_H_
#define MINIOS_SCREEN_INTERFACE_H_

#include <string>

#include "minios/screen_types.h"

namespace minios {

// `ScreenInterface` has the common functions for each Screen in miniOS. Screen
// Classes must be able to show their UI components, respond to key events, and
// reset their internal state.

class ScreenInterface {
 public:
  virtual ~ScreenInterface() = default;

  // Shows the screen and all base components.
  virtual void Show() = 0;

  // Changes the screen based on given user input. Re-shows the necessary parts
  // of the screen.
  virtual void OnKeyPress(int key_changed) = 0;

  // Resets screen state.
  virtual void Reset() = 0;

  // Gets the `ScreenType` for each screen.
  virtual ScreenType GetType() = 0;

  // Get the name of the screen as a string.
  virtual std::string GetName() = 0;
};

}  // namespace minios

#endif  // MINIOS_SCREEN_INTERFACE_H_
