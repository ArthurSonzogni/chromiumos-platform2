// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens.h"

namespace minios {

void Screens::ShowMiniOsDebugOptionsScreen() {
  MessageBaseScreen();
  const auto kX = -frecon_canvas_size_ / 2 + kDefaultMessageWidth / 2;
  const auto kY = -frecon_canvas_size_ / 2 + 220 + 18;
  ShowMessage("title_debug_options", kX, kY);
  ShowLanguageMenu(index_ == 0);

  const auto kYOffset = kY + 18 + 40;
  const auto kYStep = kButtonHeight + kButtonMargin;
  ShowButton("btn_message_log", kYOffset, index_ == 1, default_button_width_,
             false);
  ShowButton("btn_back", kYOffset + kYStep, index_ == 2, default_button_width_,
             false);
}

}  // namespace minios
