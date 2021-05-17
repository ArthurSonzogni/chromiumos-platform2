// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screen_network.h"

#include <dbus/shill/dbus-constants.h>

#include "minios/draw_utils.h"

namespace minios {

namespace {
// Dropdown size
constexpr int kNetworksPerPage = 10;
}  // namespace

ScreenNetwork::ScreenNetwork(
    std::shared_ptr<DrawInterface> draw_utils,
    std::shared_ptr<NetworkManagerInterface> network_manager,
    ScreenControllerInterface* screen_controller)
    : ScreenBase(
          /*button_count=*/3, /*index_=*/1, draw_utils, screen_controller),
      network_manager_(network_manager),
      screen_type_(ScreenType::kNetworkDropDownScreen) {
  if (network_manager_) {
    network_manager_->AddObserver(this);
    // Query for networks.
    network_manager_->GetNetworks();
  }
}

ScreenNetwork::~ScreenNetwork() {
  if (network_manager_)
    network_manager_->RemoveObserver(this);
}

void ScreenNetwork::Show() {
  draw_utils_->MessageBaseScreen();
  draw_utils_->ShowInstructions("title_MiniOS_dropdown");
  draw_utils_->ShowStepper({"1-done", "2", "3"});
  ShowButtons();
}

void ScreenNetwork::ShowButtons() {
  draw_utils_->ShowLanguageMenu(index_ == 0);
  ShowCollapsedNetworkDropDown(index_ == 1);
  draw_utils_->ShowButton("btn_back", kTitleY + 250, (index_ == 2),
                          draw_utils_->GetDefaultButtonWidth(), false);
}

void ScreenNetwork::UpdateMenu() {
  draw_utils_->ShowLanguageMenu(/*selected=*/false);
  ShowNetworkDropdown(index_);
  int items_on_page =
      std::min(kNetworksPerPage, static_cast<int>(networks_.size()));
  draw_utils_->ShowButton("btn_back", kTitleY + 250 + (items_on_page * 40),
                          (index_ == networks_.size()),
                          draw_utils_->GetDefaultButtonWidth(), false);
}

bool ScreenNetwork::IsDropDownOpen() {
  return screen_type_ == ScreenType::kExpandedNetworkDropDownScreen;
}

void ScreenNetwork::OnKeyPress(int key_changed) {
  bool enter = false;
  UpdateButtonsIndex(key_changed, &enter);

  if (enter) {
    if (!IsDropDownOpen()) {
      switch (index_) {
        case 0:
          screen_controller_->SwitchLocale(this);
          break;
        case 1:
          // No need to call screen controller. Just update the internal network
          // state.
          screen_type_ = ScreenType::kExpandedNetworkDropDownScreen;
          // Update button count for the dropdown items. Add one extra slot for
          // the back button.
          button_count_ = networks_.size() + 1;
          index_ = 0;
          UpdateMenu();
          break;
        case 2:
          screen_controller_->OnBackward(this);
          break;
      }
    } else {
      if (index_ == networks_.size()) {
        // Back button.
        screen_controller_->OnBackward(this);
      } else if (0 <= index_ && index_ < networks_.size()) {
        chosen_network_ = networks_[index_];
        LOG(INFO) << "Selected network: " << chosen_network_;
        screen_controller_->OnForward(this);
      } else {
        LOG(WARNING) << "Selected network index: " << index_
                     << " not valid. Retry";
        index_ = 0;
        UpdateMenu();
      }
    }
  } else {
    if (!IsDropDownOpen()) {
      ShowButtons();
    } else {
      UpdateMenu();
    }
  }
}

void ScreenNetwork::Reset() {
  if (IsDropDownOpen()) {
    // Reset from `kExpandedNetworkDropDownScreen` is only called when going
    // back to `kNetworkDropDownScreen`. Re-query for networks and reset
    // `ScreenType`.
    network_manager_->GetNetworks();
    screen_type_ = ScreenType::kNetworkDropDownScreen;
  }
  index_ = 1;
  button_count_ = 3;
}

ScreenType ScreenNetwork::GetType() {
  return screen_type_;
}

std::string ScreenNetwork::GetName() {
  return IsDropDownOpen() ? "ScreenExpandedNetwork" : "ScreenNetwork";
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
  if (IsDropDownOpen()) {
    button_count_ = networks_.size() + 1;
    index_ = 0;
    UpdateMenu();
  }
}

void ScreenNetwork::ShowCollapsedNetworkDropDown(bool is_selected) {
  const int frecon_canvas_size = draw_utils_->GetFreconCanvasSize();
  const int kOffsetY = -frecon_canvas_size / 2 + 350;
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
  int offset_y = -frecon_canvas_size / 2 + 350 + 40;
  const int kBackgroundX = -frecon_canvas_size / 2 + 360;
  const int kOffsetX = -frecon_canvas_size / 2 + 60;
  constexpr int kItemHeight = 40;

  if (networks_.empty()) {
    // Okay to return here as there will be a callback to refresh the dropdown
    // once the networks are found.
    draw_utils_->ShowBox(kBackgroundX, offset_y, 718, 38,
                         kMenuDropdownBackgroundBlack);
    draw_utils_->ShowText("Please wait while we find available networks.",
                          kOffsetX, offset_y, "grey");
    LOG(ERROR) << "No available networks.";
    return;
  }

  // Pick begin index such that the selected index is centered on the screen.
  // If there are not enough items for a full page then start at 0.
  int begin_index = 0;
  int page_difference = networks_.size() - kNetworksPerPage;
  if (page_difference >= 0) {
    begin_index =
        std::clamp(current_index - kNetworksPerPage / 2, 0, page_difference);
  }

  for (int i = begin_index;
       i < (begin_index + kNetworksPerPage) && i < networks_.size(); i++) {
    if (current_index == i) {
      draw_utils_->ShowBox(kBackgroundX, offset_y, 720, 40, kMenuBlue);
      draw_utils_->ShowText(networks_[i], kOffsetX, offset_y, "black");
    } else {
      draw_utils_->ShowBox(kBackgroundX, offset_y, 720, 40,
                           kMenuDropdownFrameNavy);
      draw_utils_->ShowBox(kBackgroundX, offset_y, 718, 38,
                           kMenuDropdownBackgroundBlack);
      draw_utils_->ShowText(networks_[i], kOffsetX, offset_y, "grey");
    }
    offset_y += kItemHeight;
  }
}

}  // namespace minios
