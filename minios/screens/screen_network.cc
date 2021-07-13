// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens/screen_network.h"

#include <dbus/shill/dbus-constants.h>

#include "minios/draw_utils.h"

namespace minios {

namespace {
// Dropdown Item size
constexpr int kItemHeight = 40;
}  // namespace

ScreenNetwork::ScreenNetwork(
    std::shared_ptr<DrawInterface> draw_utils,
    std::shared_ptr<NetworkManagerInterface> network_manager,
    KeyReader* key_reader,
    ScreenControllerInterface* screen_controller)
    : ScreenBase(
          /*button_count=*/3, /*index_=*/1, draw_utils, screen_controller),
      network_manager_(network_manager),
      key_reader_(key_reader),
      state_(NetworkState::kDropdownClosed) {
  if (network_manager_) {
    network_manager_->AddObserver(this);
    // Query for networks.
    network_manager_->GetNetworks();
  }
  // Calculate how much room is left for the dropdown, leave some space for the
  // back button.
  items_per_page_ =
      (draw_utils_->GetFreconCanvasSize() / 2 - kBtnYStep * 2) / kItemHeight -
      1;
}

ScreenNetwork::~ScreenNetwork() {
  if (network_manager_)
    network_manager_->RemoveObserver(this);
}

void ScreenNetwork::Show() {
  switch (state_) {
    case NetworkState::kDropdownClosed:
    case NetworkState::kDropdownOpen:
      draw_utils_->MessageBaseScreen();
      draw_utils_->ShowInstructions("title_MiniOS_dropdown");
      draw_utils_->ShowStepper({"1-done", "2", "3"});
      break;
    case NetworkState::kGetPassword:
      draw_utils_->MessageBaseScreen();
      draw_utils_->ShowInstructionsWithTitle("MiniOS_password");
      draw_utils_->ShowStepper({"done", "2-done", "3"});
      break;
  }
  ShowButtons();
}

void ScreenNetwork::ShowButtons() {
  const int frecon_canvas_size = draw_utils_->GetFreconCanvasSize();
  const int btn_width = draw_utils_->GetDefaultButtonWidth();
  const int kOffsetY = -frecon_canvas_size / 4 + kBtnYStep * 4;

  switch (state_) {
    case NetworkState::kDropdownClosed: {
      draw_utils_->ShowLanguageMenu(index_ == 0);
      ShowCollapsedNetworkDropDown(index_ == 1);
      draw_utils_->ShowButton("btn_back", kOffsetY, (index_ == 2), btn_width,
                              false);
      break;
    }
    case NetworkState::kDropdownOpen: {
      draw_utils_->ShowLanguageMenu(/*selected=*/false);
      ShowCollapsedNetworkDropDown(false);
      ShowNetworkDropdown(index_);
      int dropdown_size =
          std::min(items_per_page_, static_cast<int>(networks_.size()));
      draw_utils_->ShowButton("btn_back", kOffsetY + (dropdown_size * 40),
                              (index_ == networks_.size()), btn_width, false);
      break;
    }
    case NetworkState::kGetPassword: {
      button_count_ = 3;
      draw_utils_->ShowLanguageMenu(index_ == 0);
      const int kBtnY = (-frecon_canvas_size / 2) + 318 + kBtnYStep * 2;
      draw_utils_->ShowButton("Enter your password", kBtnY, false,
                              btn_width * 4, true);
      draw_utils_->ShowButton("btn_back", kBtnY + kBtnYStep, index_ == 2,
                              btn_width, false);
      break;
    }
  }
}

void ScreenNetwork::WaitForConnection() {
  draw_utils_->MessageBaseScreen();
  draw_utils_->ShowStepper({"done", "2-done", "3"});
  draw_utils_->ShowLanguageMenu(false);
  draw_utils_->ShowInstructions("title_MiniOS_wait_for_connection");
}

void ScreenNetwork::OnKeyPress(int key_changed) {
  bool enter = false;
  UpdateButtonsIndex(key_changed, &enter);

  if (enter) {
    if (state_ == NetworkState::kDropdownClosed) {
      switch (index_) {
        case 0:
          screen_controller_->SwitchLocale(this);
          break;
        case 1:
          // Update internal state from dropdown closed to open.
          state_ = NetworkState::kDropdownOpen;
          // Update button count for the dropdown items. Add one extra slot for
          // the back button.
          button_count_ = networks_.size() + 1;
          index_ = 0;
          Show();
          break;
        case 2:
          screen_controller_->OnBackward(this);
          break;
      }
    } else if (state_ == NetworkState::kDropdownOpen) {
      if (index_ == networks_.size()) {
        // Back button. Update internal state and re-query for networks.
        Reset();
        Show();
      } else if (0 <= index_ && index_ < networks_.size()) {
        chosen_network_ = networks_[index_];
        LOG(INFO) << "Selected network: " << chosen_network_;
        // Update internal state and get password.
        state_ = NetworkState::kGetPassword;
        index_ = 1;
        Show();
      } else {
        LOG(WARNING) << "Selected network index: " << index_
                     << " not valid. Retry";
        index_ = 0;
        ShowButtons();
      }
    } else if (state_ == NetworkState::kGetPassword) {
      switch (index_) {
        case 0:
          screen_controller_->SwitchLocale(this);
          break;
        case 1:
          GetPassword();
          break;
        case 2:
          // Back to network dropdown.
          state_ = NetworkState::kDropdownOpen;
          // Update button count for the dropdown items. Add one extra slot for
          // the back button.
          button_count_ = networks_.size() + 1;
          index_ = 0;
          chosen_network_.clear();
          Show();
          break;
      }
    }
  } else {
    // No selection made. Just update the button or menu focuses.
    ShowButtons();
  }
}

void ScreenNetwork::Reset() {
  if (state_ == NetworkState::kDropdownOpen) {
    // Reset from `kExpandedNetworkDropDownScreen` is only called when going
    // back to `kNetworkDropDownScreen`. Re-query for networks and reset
    // `ScreenType`.
    network_manager_->GetNetworks();
    state_ = NetworkState::kDropdownClosed;
  }
  index_ = 1;
  button_count_ = 3;
}

ScreenType ScreenNetwork::GetType() {
  return ScreenType::kNetworkDropDownScreen;
}

std::string ScreenNetwork::GetName() {
  switch (state_) {
    case NetworkState::kDropdownClosed:
      return "ScreenNetwork";
    case NetworkState::kDropdownOpen:
      return "ScreenExpandedNetwork";
    case NetworkState::kGetPassword:
      return "ScreenPassword";
    default:
      return "";
  }
}

void ScreenNetwork::OnGetNetworks(const std::vector<std::string>& networks,
                                  brillo::Error* error) {
  if (error) {
    LOG(ERROR) << "Could not get networks. ErrorCode=" << error->GetCode()
               << "ErrorMessage=" << error->GetMessage();
    networks_.clear();

    screen_controller_->OnError(ScreenType::kNetworkError);
    return;
  }
  LOG(INFO) << "Trying to update network list.";
  networks_ = networks;

  // If already waiting on the dropdown screen, refresh.
  if (state_ == NetworkState::kDropdownOpen) {
    button_count_ = networks_.size() + 1;
    index_ = 0;
    ShowButtons();
  }
}

void ScreenNetwork::OnConnect(const std::string& ssid, brillo::Error* error) {
  if (error) {
    LOG(ERROR) << "Could not connect to " << ssid
               << ". ErrorCode=" << error->GetCode()
               << " ErrorMessage=" << error->GetMessage();
    if (error->GetCode() == shill::kErrorResultInvalidPassphrase) {
      screen_controller_->OnError(ScreenType::kPasswordError);
    } else {
      // General network error.
      Reset();
      screen_controller_->OnError(ScreenType::kConnectionError);
    }
    return;
  }
  LOG(INFO) << "Successfully connected to " << ssid;
  screen_controller_->OnForward(this);
}

void ScreenNetwork::GetPassword() {
  const int kTitleY = (-draw_utils_->GetFreconCanvasSize() / 2) + 238;
  const int kBtnY = kTitleY + 80 + kBtnYStep * 2;
  draw_utils_->ShowButton("Begin typing", kBtnY, false,
                          draw_utils_->GetDefaultButtonWidth() * 4, true);
  CHECK(!chosen_network_.empty()) << "Cannot connect to an empty network.";
  if (!key_reader_ || !key_reader_->InputSetUp()) {
    LOG(ERROR) << "Unable to set up key reader.";
    screen_controller_->OnError(ScreenType::kGeneralError);
    return;
  }

  bool enter = false;
  bool show_password = false;
  std::string input;
  std::string plain_text_password;
  key_reader_->StopWatcher();
  do {
    if (!key_reader_->GetUserInput(&enter, &show_password, &input))
      continue;
    plain_text_password = input;
    if (!show_password) {
      input = std::string(input.size(), '*');
    }
    draw_utils_->ShowButton(input, kBtnY, false,
                            draw_utils_->GetDefaultButtonWidth() * 4, true);
  } while (!enter);
  key_reader_->StartWatcher();
  // Wait to connect to network.
  WaitForConnection();
  network_manager_->Connect(chosen_network_, plain_text_password);
}

void ScreenNetwork::ShowCollapsedNetworkDropDown(bool is_selected) {
  const int frecon_canvas_size = draw_utils_->GetFreconCanvasSize();
  const int kOffsetY = -frecon_canvas_size / 4 + kBtnYStep * 2;
  const int kBgX = -frecon_canvas_size / 2 + 145;
  const int kGlobeX = -frecon_canvas_size / 2 + 20;
  const int kArrowX = -frecon_canvas_size / 2 + 268;
  const int kTextX = -frecon_canvas_size / 2 + 100;

  base::FilePath screens_path = draw_utils_->GetScreenPath();
  // Currently using language and globe icons as placeholders.
  base::FilePath menu_background =
      is_selected ? screens_path.Append("language_menu_bg_focused.png")
                  : screens_path.Append("language_menu_bg.png");

  draw_utils_->ShowImage(menu_background, kBgX, kOffsetY);
  draw_utils_->ShowImage(screens_path.Append("ic_language-globe.png"), kGlobeX,
                         kOffsetY);
  draw_utils_->ShowImage(screens_path.Append("ic_dropdown.png"), kArrowX,
                         kOffsetY);
  draw_utils_->ShowMessage("btn_MiniOS_display_options", kTextX, kOffsetY);
}

void ScreenNetwork::ShowNetworkDropdown(int current_index) {
  const int frecon_canvas_size = draw_utils_->GetFreconCanvasSize();
  int offset_y = -frecon_canvas_size / 4 + kBtnYStep * 3;
  const int kBackgroundX = -frecon_canvas_size / 2 + 360;
  const int kOffsetX =
      -frecon_canvas_size / 2 + (draw_utils_->IsLocaleRightToLeft() ? 400 : 60);

  if (networks_.empty()) {
    // Okay to return here as there will be a callback to refresh the dropdown
    // once the networks are found.
    draw_utils_->ShowBox(kBackgroundX, offset_y, 718, 38,
                         kMenuDropdownBackgroundBlack);
    draw_utils_->ShowText("Please wait while we find available networks.",
                          kOffsetX, offset_y, "dropdown_grey");
    LOG(ERROR) << "No available networks.";
    return;
  }

  // Pick begin index such that the selected index is centered on the screen.
  // If there are not enough items for a full page then start at 0.
  int begin_index = 0;
  int page_difference = networks_.size() - items_per_page_;
  if (page_difference >= 0) {
    begin_index =
        std::clamp(current_index - items_per_page_ / 2, 0, page_difference);
  }

  for (int i = begin_index;
       i < (begin_index + items_per_page_) && i < networks_.size(); i++) {
    if (current_index == i) {
      draw_utils_->ShowBox(kBackgroundX, offset_y, 720, 40, kMenuBlue);
      draw_utils_->ShowText(networks_[i], kOffsetX, offset_y, "black");
    } else {
      draw_utils_->ShowBox(kBackgroundX, offset_y, 720, 40,
                           kMenuDropdownFrameNavy);
      draw_utils_->ShowBox(kBackgroundX, offset_y, 718, 38,
                           kMenuDropdownBackgroundBlack);
      draw_utils_->ShowText(networks_[i], kOffsetX, offset_y, "dropdown_grey");
    }
    offset_y += kItemHeight;
  }
}

}  // namespace minios
