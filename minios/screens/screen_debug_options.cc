// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens/screen_debug_options.h"

#include <base/logging.h>

#include "minios/draw_utils.h"

namespace minios {
// TODO(b/191139789): minios: clean up, combine generic screens into one.
ScreenDebugOptions::ScreenDebugOptions(
    std::shared_ptr<DrawInterface> draw_utils,
    ScreenControllerInterface* screen_controller)
    : ScreenBase(
          /*button_count=*/3, /*index_=*/1, draw_utils, screen_controller) {}

void ScreenDebugOptions::Show() {
  draw_utils_->MessageBaseScreen();
  int frecon_size = draw_utils_->GetFreconCanvasSize();
  const auto kX = -frecon_size / 2 + kDefaultMessageWidth / 2;
  const auto kY = -frecon_size / 2 + 220 + 18;
  draw_utils_->ShowMessage("title_debug_options", kX, kY);
  ShowButtons();
}

void ScreenDebugOptions::ShowButtons() {
  draw_utils_->ShowLanguageMenu(index_ == 0);
  int default_width = draw_utils_->GetDefaultButtonWidth();
  const int kYOffset = -100;
  const int kYStep = kButtonHeight + kButtonMargin;
  draw_utils_->ShowButton("btn_message_log", kYOffset, index_ == 1,
                          default_width, false);
  draw_utils_->ShowButton("btn_back", kYOffset + kYStep, index_ == 2,
                          default_width, false);
}

void ScreenDebugOptions::OnKeyPress(int key_changed) {
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

void ScreenDebugOptions::Reset() {
  index_ = 1;
}

ScreenType ScreenDebugOptions::GetType() {
  return ScreenType::kDebugOptionsScreen;
}

std::string ScreenDebugOptions::GetName() {
  return "ScreenDebugOptions";
}

}  // namespace minios
