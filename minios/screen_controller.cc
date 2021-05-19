// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screen_controller.h"

#include <utility>

#include <base/logging.h>

#include "minios/screen_language_dropdown.h"
#include "minios/screen_network.h"
#include "minios/screen_welcome.h"

namespace minios {

ScreenController::ScreenController(std::shared_ptr<DrawInterface> draw_utils)
    : key_reader_(KeyReader{/*include_usb=*/true}),
      draw_utils_(draw_utils),
      key_states_(kFdsMax, std::vector<bool>(kKeyMax, false)) {}

void ScreenController::Init() {
  CHECK(draw_utils_)
      << "Screen drawing utility not available. Cannot continue.";

  draw_utils_->Init();

  std::vector<int> wait_keys = {kKeyUp, kKeyDown, kKeyEnter};
  if (draw_utils_->IsDetachable())
    wait_keys = {kKeyVolDown, kKeyVolUp, kKeyPower};
  CHECK(key_reader_.Init(wait_keys))
      << "Could not initialize key reader. Unable to continue.";

  key_reader_.SetDelegate(this);

  current_screen_ = CreateScreen(ScreenType::kWelcomeScreen);
  current_screen_->Show();
}

std::unique_ptr<ScreenInterface> ScreenController::CreateScreen(
    ScreenType screen) {
  switch (screen) {
    case ScreenType::kWelcomeScreen:
      return std::make_unique<ScreenWelcome>(draw_utils_, this);
    case ScreenType::kNetworkDropDownScreen:
      return std::make_unique<ScreenNetwork>(draw_utils_, this);
    case ScreenType::kLanguageDropDownScreen:
      return std::make_unique<ScreenLanguageDropdown>(draw_utils_, this);
    default:
      // TODO(vyshu) : Other screens not yet implemented. Once all screens are
      // done, this should never return nullptr.
      return nullptr;
  }
}

void ScreenController::OnForward(ScreenInterface* screen) {
  switch (screen->GetType()) {
    case ScreenType::kWelcomeScreen:
      current_screen_ = CreateScreen(ScreenType::kNetworkDropDownScreen);
      break;
    default:
      // TODO(vyshu) : Other screens not yet implemented.
      break;
  }
  current_screen_->Show();
}

void ScreenController::OnBackward(ScreenInterface* screen) {
  switch (screen->GetType()) {
    case ScreenType::kWelcomeScreen:
      current_screen_->Reset();
      break;
    case ScreenType::kNetworkDropDownScreen:
      current_screen_ = CreateScreen(ScreenType::kWelcomeScreen);
      break;
    default:
      // TODO(vyshu) : Other screens not yet implemented.
      break;
  }
  current_screen_->Show();
}

void ScreenController::SwitchLocale(ScreenInterface* screen) {
  previous_screen_ = std::move(current_screen_);
  current_screen_ = CreateScreen(ScreenType::kLanguageDropDownScreen);
  current_screen_->Show();
}

void ScreenController::UpdateLocale(ScreenInterface* screen,
                                    int selected_locale_index) {
  // Change locale and update constants.
  CHECK(draw_utils_) << "Screen drawing utility not available.";
  draw_utils_->LocaleChange(selected_locale_index);
  if (screen->GetType() != ScreenType::kLanguageDropDownScreen) {
    LOG(WARNING) << "Only the language dropdown screen can change the locale.";
    return;
  }
  current_screen_ = std::move(previous_screen_);
  current_screen_->Reset();
  current_screen_->Show();
}

void ScreenController::OnKeyPress(int fd_index,
                                  int key_changed,
                                  bool key_released) {
  CHECK(current_screen_) << "Could not send key event to screen.";

  // Make sure you have seen a key press for this key before ending on key
  // event release.
  if (fd_index < 0 || key_changed < 0 || fd_index >= key_states_.size() ||
      key_changed >= key_states_[0].size()) {
    LOG(ERROR) << "Fd index or key code out of range.  Index: " << fd_index
               << ". Key code: " << key_changed;
    return;
  }

  if (key_released && key_states_[fd_index][key_changed]) {
    key_states_[fd_index][key_changed] = false;
    // Send key event to the currently displayed screen. It will decide what to
    // do with it.
    current_screen_->OnKeyPress(key_changed);
    return;
  } else if (!key_released) {
    key_states_[fd_index][key_changed] = true;
  }
}

}  // namespace minios
