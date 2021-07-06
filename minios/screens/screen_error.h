// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREENS_SCREEN_ERROR_H_
#define MINIOS_SCREENS_SCREEN_ERROR_H_

#include <memory>
#include <string>

#include "minios/screens/screen_base.h"

namespace minios {

class ScreenError : public ScreenBase {
 public:
  ScreenError(ScreenType error_screen,
              std::shared_ptr<DrawInterface> draw_utils,
              ScreenControllerInterface* screen_controller);

  ~ScreenError() = default;

  ScreenError(const ScreenError&) = delete;
  ScreenError& operator=(const ScreenError&) = delete;

  void Show() override;
  void Reset() override;
  void OnKeyPress(int key_changed) override;
  ScreenType GetType() override;
  std::string GetName() override;

 private:
  // Updates buttons with current selection.
  void ShowButtons();

  // Gets the title message based on the `error_screen_`.
  std::string GetErrorMessage();

  ScreenType error_screen_;
};

}  // namespace minios

#endif  // MINIOS_SCREENS_SCREEN_ERROR_H_
