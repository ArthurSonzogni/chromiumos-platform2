// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens/screen_permission.h"

#include "minios/draw_utils.h"

namespace minios {
// TODO(b/191139789): minios: clean up, combine generic screens into one.
ScreenPermission::ScreenPermission(std::shared_ptr<DrawInterface> draw_utils,
                                   ScreenControllerInterface* screen_controller)
    : ScreenBase(
          /*button_count=*/3, /*index_=*/1, draw_utils, screen_controller) {}

void ScreenPermission::Show() {
  draw_utils_->MessageBaseScreen();
  draw_utils_->ShowInstructionsWithTitle("MiniOS_user_confirm");
  draw_utils_->ShowStepper({"done", "2-done", "3-done"});
  ShowButtons();
}

void ScreenPermission::ShowButtons() {
  draw_utils_->ShowLanguageMenu(index_ == 0);
  const int kBtnY =
      (-draw_utils_->GetFreconCanvasSize() / 2) + 318 + kBtnYStep * 2;
  int default_width = draw_utils_->GetDefaultButtonWidth();
  draw_utils_->ShowButton("btn_next", kBtnY, (index_ == 1), default_width,
                          false);
  draw_utils_->ShowButton("btn_back", kBtnY + kBtnYStep, (index_ == 2),
                          default_width, false);
}

void ScreenPermission::OnKeyPress(int key_changed) {
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
      default:
        LOG(FATAL) << "Index " << index_ << " is not valid.";
    }
  } else {
    ShowButtons();
  }
}

void ScreenPermission::Reset() {
  index_ = 1;
}

ScreenType ScreenPermission::GetType() {
  return ScreenType::kUserPermissionScreen;
}

std::string ScreenPermission::GetName() {
  return "ScreenUserPermission";
}

}  // namespace minios
