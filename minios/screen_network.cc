// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screen_network.h"

namespace minios {

ScreenNetwork::ScreenNetwork(std::shared_ptr<DrawInterface> draw_utils,
                             ScreenControllerInterface* screen_controller)
    : ScreenBase(
          /*button_count=*/3, /*index_=*/1, draw_utils, screen_controller) {}

void ScreenNetwork::Show() {
  draw_utils_->MessageBaseScreen();
  draw_utils_->ShowInstructions("title_MiniOS_dropdown");
  draw_utils_->ShowStepper({"1-done", "2", "3"});
  ShowButtons();
}

void ScreenNetwork::ShowButtons() {
  draw_utils_->ShowLanguageMenu(index_ == 0);
  draw_utils_->ShowCollapsedNetworkDropDown((index_ == 1));
  draw_utils_->ShowButton("btn_back", kTitleY + 58 + (4 * kBtnYStep),
                          (index_ == 2), draw_utils_->GetDefaultButtonWidth(),
                          false);
}

void ScreenNetwork::OnKeyPress(int key_changed) {
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
      case 2:
        screen_controller_->OnBackward(this);
        break;
    }
  } else {
    ShowButtons();
  }
}

void ScreenNetwork::Reset() {
  index_ = 1;
}

ScreenType ScreenNetwork::GetType() {
  return ScreenType::kNetworkDropDownScreen;
}

std::string ScreenNetwork::GetName() {
  return "ScreenNetwork";
}

}  // namespace minios
