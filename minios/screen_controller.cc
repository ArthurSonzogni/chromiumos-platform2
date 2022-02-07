// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screen_controller.h"

#include <utility>

#include <base/logging.h>

#include "minios/recovery_installer.h"
#include "minios/screens/screen_debug_options.h"
#include "minios/screens/screen_download.h"
#include "minios/screens/screen_error.h"
#include "minios/screens/screen_language_dropdown.h"
#include "minios/screens/screen_log.h"
#include "minios/screens/screen_network.h"
#include "minios/screens/screen_permission.h"
#include "minios/screens/screen_welcome.h"
#include "minios/utils.h"

namespace minios {

ScreenController::ScreenController(
    std::shared_ptr<DrawInterface> draw_utils,
    std::shared_ptr<UpdateEngineProxy> update_engine_proxy,
    std::shared_ptr<NetworkManagerInterface> network_manager,
    ProcessManagerInterface* process_manager)
    : draw_utils_(draw_utils),
      update_engine_proxy_(update_engine_proxy),
      network_manager_(network_manager),
      process_manager_(process_manager),
      key_reader_(
          KeyReader{/*include_usb=*/true, GetKeyboardLayout(process_manager_)}),
      key_states_(kFdsMax, std::vector<bool>(kKeyMax, false)) {}

bool ScreenController::Init() {
  if (!draw_utils_ || !draw_utils_->Init()) {
    LOG(ERROR) << "Screen drawing utility not available. Cannot continue.";
    return false;
  }
  update_engine_proxy_->Init();

  std::vector<int> wait_keys = {KEY_UP, KEY_DOWN, KEY_ENTER, KEY_ESC};
  if (draw_utils_->IsDetachable())
    wait_keys = {KEY_VOLUMEDOWN, KEY_VOLUMEUP, KEY_POWER, KEY_ESC};
  if (!key_reader_.Init(wait_keys)) {
    LOG(ERROR) << "Could not initialize key reader. Unable to continue.";
    return false;
  }

  key_reader_.SetDelegate(this);

  current_screen_ = CreateScreen(ScreenType::kWelcomeScreen);
  current_screen_->Show();
  return true;
}

std::unique_ptr<ScreenInterface> ScreenController::CreateScreen(
    ScreenType screen_type) {
  switch (screen_type) {
    case ScreenType::kWelcomeScreen:
      return std::make_unique<ScreenWelcome>(draw_utils_, this);
    case ScreenType::kNetworkDropDownScreen:
      return std::make_unique<ScreenNetwork>(draw_utils_, network_manager_,
                                             &key_reader_, this);
    case ScreenType::kLanguageDropDownScreen:
      return std::make_unique<ScreenLanguageDropdown>(draw_utils_, this);
    case ScreenType::kUserPermissionScreen:
      return std::make_unique<ScreenPermission>(draw_utils_, this);
    case ScreenType::kStartDownload:
      return std::make_unique<ScreenDownload>(
          std::make_unique<RecoveryInstaller>(process_manager_),
          update_engine_proxy_, draw_utils_, this);
    case ScreenType::kDownloadError:
    case ScreenType::kNetworkError:
    case ScreenType::kPasswordError:
    case ScreenType::kConnectionError:
    case ScreenType::kGeneralError:
      return std::make_unique<ScreenError>(screen_type, draw_utils_, this);
    case ScreenType::kDebugOptionsScreen:
      return std::make_unique<ScreenDebugOptions>(draw_utils_, this);
    case ScreenType::kLogScreen:
      return std::make_unique<ScreenLog>(draw_utils_, this);
    default:
      LOG(FATAL) << "Invalid screen.";
      return nullptr;
  }
}

void ScreenController::OnForward(ScreenInterface* screen) {
  switch (screen->GetType()) {
    case ScreenType::kWelcomeScreen:
      current_screen_ = CreateScreen(ScreenType::kNetworkDropDownScreen);
      break;
    case ScreenType::kNetworkDropDownScreen:
      current_screen_ = CreateScreen(ScreenType::kUserPermissionScreen);
      break;
    case ScreenType::kUserPermissionScreen:
      current_screen_ = CreateScreen(ScreenType::kStartDownload);
      break;
    case ScreenType::kDownloadError:
    case ScreenType::kNetworkError:
    case ScreenType::kPasswordError:
    case ScreenType::kConnectionError:
    case ScreenType::kGeneralError:
      // Show debug options and log screen. Save error screen to return to from
      // `kDebugOptionsScreen`.
      previous_screen_ = std::move(current_screen_);
      current_screen_ = CreateScreen(ScreenType::kDebugOptionsScreen);
      break;
    case ScreenType::kDebugOptionsScreen:
      current_screen_ = CreateScreen(ScreenType::kLogScreen);
      break;
    default:
      LOG(FATAL) << "Invalid screen.";
  }
  current_screen_->Show();
}

void ScreenController::OnBackward(ScreenInterface* screen) {
  switch (screen->GetType()) {
    case ScreenType::kWelcomeScreen:
    case ScreenType::kNetworkDropDownScreen:
    case ScreenType::kUserPermissionScreen:
      previous_screen_ = nullptr;
      current_screen_ = CreateScreen(ScreenType::kWelcomeScreen);
      break;
    case ScreenType::kPasswordError:
    case ScreenType::kNetworkError:
    case ScreenType::kConnectionError:
      // Return to network dropdown screen.
      previous_screen_ = nullptr;
      current_screen_ = CreateScreen(ScreenType::kNetworkDropDownScreen);
      break;
    case ScreenType::kDownloadError:
    case ScreenType::kGeneralError:
      // Return to beginning of the flow.
      previous_screen_ = nullptr;
      current_screen_ = CreateScreen(ScreenType::kWelcomeScreen);
      break;
    case ScreenType::kDebugOptionsScreen:
      // Back to original error screen, reset index.
      if (previous_screen_ &&
          dynamic_cast<ScreenError*>(previous_screen_.get())) {
        current_screen_ = std::move(previous_screen_);
        current_screen_->Reset();
      } else {
        // No error screen saved. Go back to beginning.
        previous_screen_ = nullptr;
        current_screen_ = CreateScreen(ScreenType::kWelcomeScreen);
      }
      break;
    case ScreenType::kLogScreen:
      // Back to debug options screen.
      current_screen_ = CreateScreen(ScreenType::kDebugOptionsScreen);
      break;
    case ScreenType::kLanguageDropDownScreen:
      if (previous_screen_) {
        current_screen_ = std::move(previous_screen_);
      } else {
        current_screen_ = CreateScreen(ScreenType::kWelcomeScreen);
      }
      break;
    default:
      LOG(FATAL) << "Invalid screen.";
  }
  current_screen_->Show();
}

void ScreenController::OnError(ScreenType error_screen) {
  switch (error_screen) {
    case ScreenType::kDownloadError:
    case ScreenType::kNetworkError:
    case ScreenType::kPasswordError:
    case ScreenType::kConnectionError:
    case ScreenType::kGeneralError:
      previous_screen_ = std::move(current_screen_);
      current_screen_ = CreateScreen(error_screen);
      break;
    default:
      LOG(WARNING)
          << "Not a valid error screen. Defaulting to general error case.";
      previous_screen_ = std::move(current_screen_);
      current_screen_ = CreateScreen(ScreenType::kGeneralError);
      break;
  }
  current_screen_->Show();
}

ScreenType ScreenController::GetCurrentScreen() {
  return current_screen_->GetType();
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
  if (screen->GetType() != ScreenType::kLanguageDropDownScreen) {
    LOG(WARNING) << "Only the language dropdown screen can change the locale.";
    return;
  }
  draw_utils_->LocaleChange(selected_locale_index);
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
