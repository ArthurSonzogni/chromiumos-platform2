// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens/screen_welcome.h"

namespace minios {

ScreenWelcome::ScreenWelcome(std::shared_ptr<DrawInterface> draw_utils,
                             ScreenControllerInterface* screen_controller)
    : ScreenBase(
          /*button_count=*/2, /*index_=*/1, draw_utils, screen_controller) {}

void ScreenWelcome::Show() {
  draw_utils_->MessageBaseScreen();
  draw_utils_->ShowInstructionsWithTitle("MiniOS_welcome");
  draw_utils_->ShowStepper({"1-done", "2", "3"});
  ShowButtons();
}

void ScreenWelcome::ShowButtons() {
  draw_utils_->ShowLanguageMenu(index_ == 0);
  const int kBtnY =
      (-draw_utils_->GetFreconCanvasSize() / 2) + 318 + kBtnYStep * 2;
  draw_utils_->ShowButton("btn_next", kBtnY, (index_ == 1),
                          draw_utils_->GetDefaultButtonWidth(), false);
}

void ScreenWelcome::OnKeyPress(int key_changed) {
  bool enter = false;
  UpdateButtonsIndex(key_changed, &enter);
  if (enter) {
    switch (index_) {
      case 0:
        screen_controller_->SwitchLocale(this);
        break;
      case 1:
        screen_controller_->OnForward(this);
        break;
    }
  } else {
    ShowButtons();
  }
}

void ScreenWelcome::Reset() {
  index_ = 1;
}

ScreenType ScreenWelcome::GetType() {
  return ScreenType::kWelcomeScreen;
}

std::string ScreenWelcome::GetName() {
  return "ScreenWelcome";
}

}  // namespace minios
