// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens/screen_error.h"

#include <base/logging.h>

#include "minios/draw_utils.h"

namespace minios {

ScreenError::ScreenError(ScreenType error_screen,
                         std::shared_ptr<DrawInterface> draw_utils,
                         ScreenControllerInterface* screen_controller)
    : ScreenBase(
          /*button_count=*/3, /*index_=*/1, draw_utils, screen_controller),
      error_screen_(error_screen) {}

std::string ScreenError::GetErrorMessage() {
  switch (error_screen_) {
    case ScreenType::kDownloadError:
      return "MiniOS_download_error";
    case ScreenType::kNetworkError:
      return "MiniOS_network_error";
    case ScreenType::kPasswordError:
      return "MiniOS_password_error";
    case ScreenType::kConnectionError:
      return "MiniOS_connection_error";
    case ScreenType::kGeneralError:
      return "MiniOS_general_error";
    default:
      LOG(FATAL) << "Not a valid error screen.";
      return "";
  }
}

void ScreenError::Show() {
  draw_utils_->MessageBaseScreen();
  std::string error_message = GetErrorMessage();

  base::FilePath error_path_title =
      draw_utils_->GetScreenPath().Append("en-US").Append(
          "title_" + error_message + ".png");
  base::FilePath error_path_desc =
      draw_utils_->GetScreenPath().Append("en-US").Append(
          "desc_" + error_message + ".png");
  if (!base::PathExists(error_path_title) ||
      !base::PathExists(error_path_desc)) {
    LOG(WARNING) << "Could not find error " << error_message;
    error_message = "MiniOS_general_error";
  }

  draw_utils_->ShowInstructionsWithTitle(error_message);
  ShowButtons();
}

void ScreenError::ShowButtons() {
  draw_utils_->ShowLanguageMenu(index_ == 0);
  const int kBtnY =
      (-draw_utils_->GetFreconCanvasSize() / 2) + 318 + kBtnYStep * 2;
  draw_utils_->ShowButton("btn_try_again", kBtnY, index_ == 1,
                          draw_utils_->GetDefaultButtonWidth(), false);
  draw_utils_->ShowButton("btn_debug_options", kBtnY + kBtnYStep, index_ == 2,
                          draw_utils_->GetDefaultButtonWidth(), false);
}

void ScreenError::OnKeyPress(int key_changed) {
  bool enter = false;
  UpdateButtonsIndex(key_changed, &enter);
  if (enter) {
    switch (index_) {
      case 0:
        screen_controller_->SwitchLocale(this);
        break;
      case 1:
        screen_controller_->OnBackward(this);
        break;
      case 2:
        screen_controller_->OnForward(this);
        break;
      default:
        LOG(FATAL) << "Index " << index_ << " is not valid.";
    }
  } else {
    ShowButtons();
  }
}

void ScreenError::Reset() {
  index_ = 1;
}

ScreenType ScreenError::GetType() {
  return error_screen_;
}

std::string ScreenError::GetName() {
  switch (error_screen_) {
    case ScreenType::kDownloadError:
      return "ScreenDownloadError";
    case ScreenType::kNetworkError:
      return "ScreenNetworkError";
    case ScreenType::kPasswordError:
      return "ScreenPasswordError";
    case ScreenType::kConnectionError:
      return "ScreenConnectionError";
    case ScreenType::kGeneralError:
      return "ScreenGeneralError";
    default:
      LOG(ERROR) << "Not a valid error screen.";
      return "";
  }
}

}  // namespace minios
