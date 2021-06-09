// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens.h"

#include <algorithm>
#include <utility>

#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/strings/string_number_conversions.h>
#include <base/values.h>
#include <brillo/message_loops/message_loop.h>
#include <dbus/shill/dbus-constants.h>

#include "minios/minios.h"
#include "minios/utils.h"

namespace minios {

namespace {
// Buttons Spacing
constexpr int kTitleY = (-1080 / 2) + 238;
constexpr int kBtnYStep = 40;

// Dropdown size.
constexpr int kNetworksPerPage = 10;

constexpr char kLogPath[] = "/var/log/messages";
}  // namespace

bool Screens::Init() {
  DrawUtils::Init();
  IsDetachable();

  // Add size of language dropdown menu using the number of locales.
  menu_count_[ScreenType::kLanguageDropDownScreen] = GetSupportedLocalesSize();

  std::vector<int> wait_keys;
  if (!is_detachable_)
    wait_keys = {kKeyUp, kKeyDown, kKeyEnter};
  else
    wait_keys = {kKeyVolDown, kKeyVolUp, kKeyPower};
  if (!key_reader_.Init(wait_keys)) {
    LOG(ERROR) << "Could not initialize key reader. Unable to continue. ";
    return false;
  }
  return true;
}

bool Screens::InitForTest() {
  screens_path_ = root_.Append(kScreens);
  ReadDimensionConstants();
  return true;
}

void Screens::StartMiniOsFlow() {
  index_ = 1;
  return ShowMiniOsWelcomeScreen();
}

void Screens::LanguageMenuOnSelect() {
  ShowLanguageMenu(false);
  int start_index = FindLocaleIndex(index_);
  ShowLanguageDropdown(start_index);
}

void Screens::ShowMiniOsWelcomeScreen() {
  MessageBaseScreen();
  ShowInstructionsWithTitle("MiniOS_welcome");
  ShowStepper({"1", "2", "3"});

  ShowLanguageMenu(index_ == 0);
  constexpr int kBtnY = kTitleY + 80 + kBtnYStep * 2;
  ShowButton("btn_next", kBtnY, (index_ == 1), default_button_width_, false);
  ShowButton("btn_back", kBtnY + kBtnYStep, (index_ == 2),
             default_button_width_, false);
}

void Screens::ShowMiniOsNetworkDropdownScreen() {
  MessageBaseScreen();
  ShowInstructions("title_MiniOS_dropdown");
  ShowStepper({"1-done", "2", "3"});
  ShowLanguageMenu(index_ == 0);
  ShowCollapsedNetworkDropDown((index_ == 1));
  ShowButton("btn_back", kTitleY + 58 + (4 * kBtnYStep), (index_ == 2),
             default_button_width_, false);
}

void Screens::ExpandNetworkDropdown() {
  ShowInstructions("title_MiniOS_dropdown");
  ShowStepper({"1-done", "2", "3"});
  ShowLanguageMenu(false);
  ShowCollapsedNetworkDropDown(true);

  ShowNetworkDropdown();
  int items_on_page =
      std::min(kNetworksPerPage, static_cast<int>(network_list_.size()));
  ShowButton("btn_back", -frecon_canvas_size_ / 2 + 450 + (items_on_page * 40),
             (index_ == network_list_.size()), default_button_width_, false);
}

void Screens::ShowMiniOsGetPasswordScreen() {
  MessageBaseScreen();
  ShowInstructionsWithTitle("MiniOS_password");
  ShowStepper({"done", "2-done", "3"});
  ShowLanguageMenu(index_ == 0);
  constexpr int kBtnY = kTitleY + 58 + kBtnYStep * 2;
  ShowButton("Enter your password", kBtnY, false, default_button_width_ * 4,
             true);
  ShowButton("btn_back", kBtnY + kBtnYStep, (index_ == 2),
             default_button_width_, false);
}

void Screens::GetPassword() {
  ProcessManager process_manager;
  KeyReader password_key_reader =
      KeyReader(/*include_usb=*/true, GetVpdRegion(root_, &process_manager));
  password_key_reader.InputSetUp();

  constexpr int kBtnY = kTitleY + 58 + kBtnYStep * 2;
  ShowButton("", kBtnY, false, default_button_width_ * 4, true);

  bool enter = false;
  bool show_password = false;
  std::string input;
  std::string plain_text_password;
  do {
    if (!password_key_reader.GetUserInput(&enter, &show_password, &input))
      continue;
    plain_text_password = input;
    if (!show_password) {
      input = std::string(input.size(), '*');
    }
    ShowButton(input, kBtnY, false, default_button_width_ * 4, true);
  } while (!enter);
  // TODO(vyshu) : Logging password for development purposes only. Remove.
  LOG(INFO) << "User password is: " << plain_text_password;

  // Wait to connect to network.
  current_screen_ = ScreenType::kWaitForConnection;
  ShowNewScreen();
  network_manager_->Connect(chosen_network_, plain_text_password);
}

void Screens::ShowWaitingForConnectionScreen() {
  MessageBaseScreen();
  ShowStepper({"done", "2-done", "3-done"});
  ShowLanguageMenu(false);
  ShowInstructions("title_MiniOS_wait_for_connection");
}

void Screens::ShowUserPermissionScreen() {
  MessageBaseScreen();
  ShowInstructionsWithTitle("MiniOS_user_confirm");
  ShowStepper({"done", "2-done", "3-done"});

  ShowLanguageMenu(index_ == 0);
  constexpr int kBtnY = kTitleY + 80 + kBtnYStep * 2;
  ShowButton("btn_next", kBtnY, (index_ == 1), default_button_width_, false);
  ShowButton("btn_back", kBtnY + kBtnYStep, (index_ == 2),
             default_button_width_, false);
}

void Screens::ShowMiniOsDownloadingScreen() {
  MessageBaseScreen();
  ShowInstructionsWithTitle("MiniOS_downloading");
  ShowStepper({"done", "done", "3-done"});
  ShowLanguageMenu(false);
  constexpr int kProgressHeight = 4;
  ShowBox(0, 0, 1000, kProgressHeight, kMenuGrey);
}

void Screens::ShowMiniOsCompleteScreen() {
  MessageBaseScreen();
  ShowInstructions("title_MiniOS_complete");
  ShowStepper({"done", "done", "done"});
  ShowLanguageMenu(false);

  update_engine_proxy_->TriggerReboot();
}

void Screens::ShowErrorScreen(std::string error_message) {
  MessageBaseScreen();
  base::FilePath error_path_title =
      screens_path_.Append(locale_).Append("title_" + error_message + ".png");
  base::FilePath error_path_desc =
      screens_path_.Append(locale_).Append("desc_" + error_message + ".png");
  if (!base::PathExists(error_path_title) ||
      !base::PathExists(error_path_desc)) {
    LOG(WARNING) << "Could not find error " << error_message;
    error_message = "MiniOS_general_error";
  }
  ShowInstructionsWithTitle(error_message);
  ShowStepper({"done", "done", "stepper_error"});
  ShowLanguageMenu(index_ == 0);
  const int kYOffset = -100;
  const int kYStep = kButtonHeight + kButtonMargin;
  ShowButton("btn_try_again", kYOffset, index_ == 1, default_button_width_,
             false);
  ShowButton("btn_debug_options", kYOffset + kYStep, index_ == 2,
             default_button_width_, false);
}

void Screens::ChangeToErrorScreen(enum ScreenType error_screen) {
  current_screen_ = error_screen;
  display_update_engine_state_ = false;
  index_ = 1;
  ShowNewScreen();
}

void Screens::UpdateButtons(int menu_count, int key, bool* enter) {
  int starting_index = index_;
  // Make sure index is in range, if not reset to 0.
  if (starting_index < 0 || starting_index >= menu_count)
    starting_index = 0;

  // Modify selected index and enter state based on user key input.
  if (key == kKeyUp || key == kKeyVolUp) {
    if (starting_index > 0) {
      starting_index--;
    }
  } else if (key == kKeyDown || key == kKeyVolDown) {
    if (starting_index < (menu_count - 1)) {
      starting_index++;
    }
  } else if (key == kKeyEnter || key == kKeyPower) {
    *enter = true;
  } else {
    LOG(ERROR) << "Unknown key value: " << key;
  }
  index_ = starting_index;
}

void Screens::ShowNetworkDropdown() {
  int offset_y = -frecon_canvas_size_ / 2 + 350 + 40;
  const int kBackgroundX = -frecon_canvas_size_ / 2 + 360;
  const int kOffsetX = -frecon_canvas_size_ / 2 + 60;
  constexpr int kItemHeight = 40;

  if (network_list_.empty()) {
    // Okay to return here as there will be a callback to refresh the dropdown
    // once the networks are found.
    ShowBox(kBackgroundX, offset_y, 718, 38, kMenuDropdownBackgroundBlack);
    ShowText("Please wait while we find available networks.", kOffsetX,
             offset_y, "grey");
    LOG(ERROR) << "No available networks.";
    return;
  }

  // Pick begin index such that the selected index is centered on the screen.
  // If there are not enough items for a full page then start at 0.
  int begin_index = 0;
  int page_difference = network_list_.size() - kNetworksPerPage;
  if (page_difference >= 0) {
    begin_index = std::clamp(index_ - kNetworksPerPage / 2, 0, page_difference);
  }

  for (int i = begin_index;
       i < (begin_index + kNetworksPerPage) && i < network_list_.size(); i++) {
    if (index_ == i) {
      ShowBox(kBackgroundX, offset_y, 720, 40, kMenuBlue);
      ShowText(network_list_[i], kOffsetX, offset_y, "black");
    } else {
      ShowBox(kBackgroundX, offset_y, 720, 40, kMenuDropdownFrameNavy);
      ShowBox(kBackgroundX, offset_y, 718, 38, kMenuDropdownBackgroundBlack);
      ShowText(network_list_[i], kOffsetX, offset_y, "grey");
    }
    offset_y += kItemHeight;
  }
}

void Screens::OnKeyPress(int fd_index, int key_changed, bool key_released) {
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
    bool enter = false;
    UpdateButtons(menu_count_[current_screen_], key_changed, &enter);
    SwitchScreen(enter);
    return;
  } else if (!key_released) {
    key_states_[fd_index][key_changed] = true;
  }
}

void Screens::SwitchScreen(bool enter) {
  // Changing locale. Remember the current screen to return back to it.
  if (enter && index_ == 0 &&
      current_screen_ != ScreenType::kLanguageDropDownScreen &&
      current_screen_ != ScreenType::kExpandedNetworkDropDownScreen &&
      current_screen_ != ScreenType::kWaitForConnection &&
      current_screen_ != ScreenType::kStartDownload) {
    previous_screen_ = current_screen_;
    current_screen_ = ScreenType::kLanguageDropDownScreen;
    LanguageMenuOnSelect();
    return;
  }

  // Not switching to a different screen. Just update `current_screen_` with the
  // new index.
  if (!enter) {
    switch (current_screen_) {
      case ScreenType::kLogScreen:
        UpdateLogScreenButtons();
        return;
      default:
        break;
    }
    ShowNewScreen();
    return;
  }

  switch (current_screen_) {
    case ScreenType::kWelcomeScreen:
      if (index_ == 1) {
        current_screen_ = ScreenType::kNetworkDropDownScreen;
        // Update available networks every time the dropdown screen is picked.
        // TODO(vyshu): Change this to only update networks when necessary.
        UpdateNetworkList();
      }
      index_ = 1;
      break;
    case ScreenType::kNetworkDropDownScreen:
      if (index_ == 1) {
        index_ = 0;
        current_screen_ = ScreenType::kExpandedNetworkDropDownScreen;
        MessageBaseScreen();
      } else {
        index_ = 1;
        current_screen_ = ScreenType::kWelcomeScreen;
      }
      break;
    case ScreenType::kExpandedNetworkDropDownScreen:
      if (index_ == menu_count_[current_screen_] - 1) {
        index_ = 1;
        current_screen_ = ScreenType::kWelcomeScreen;
      } else if (network_list_.size() > index_ && index_ >= 0) {
        chosen_network_ = network_list_[index_];
        index_ = 1;
        current_screen_ = ScreenType::kPasswordScreen;
      } else {
        LOG(WARNING) << "Selected network index: " << index_
                     << " not valid. Retry";
        index_ = 0;
      }
      break;
    case ScreenType::kPasswordScreen:
      if (index_ == 1) {
        GetPassword();
      } else {
        index_ = 1;
        current_screen_ = ScreenType::kNetworkDropDownScreen;
        UpdateNetworkList();
      }
      break;
    case ScreenType::kLanguageDropDownScreen:
      if (enter) {
        current_screen_ = previous_screen_;
        LocaleChange(index_);
        index_ = 1;
        SwitchScreen(false);
        return;
      }
      break;
    case ScreenType::kUserPermissionScreen:
      if (index_ == 1) {
        // User has confirmed, start recovery and display Download
        // screen while some blocking tasks run.
        brillo::MessageLoop::current()->PostTask(
            FROM_HERE, base::Bind(&Screens::OnUserPermission,
                                  weak_ptr_factory_.GetWeakPtr()));
        index_ = 0;
        current_screen_ = ScreenType::kStartDownload;
      } else {
        // Permission denied, go back.
        index_ = 1;
        current_screen_ = ScreenType::kWelcomeScreen;
      }
      break;
    case ScreenType::kWaitForConnection:
    case ScreenType::kStartDownload:
      return;
    case ScreenType::kGeneralError:
    case ScreenType::kDownloadError:
      if (index_ == 1) {
        // Back to beginning.
        current_screen_ = ScreenType::kWelcomeScreen;
      } else if (index_ == 2) {
        index_ = 1;
        current_screen_ = ScreenType::kDebugOptionsScreen;
      }
      break;
    case ScreenType::kPasswordError:
    case ScreenType::kNetworkError:
    case ScreenType::kConnectionError:
      if (index_ == 1) {
        // Back to dropdown screen,
        current_screen_ = ScreenType::kNetworkDropDownScreen;
      } else if (index_ == 2) {
        index_ = 1;
        current_screen_ = ScreenType::kDebugOptionsScreen;
      }
      break;
    case ScreenType::kDebugOptionsScreen:
      if (index_ == 1) {
        log_path_ = base::FilePath(kLogPath);
        log_offset_idx_ = 0;
        log_offsets_ = {0};
        current_screen_ = ScreenType::kLogScreen;
      } else if (index_ == 2) {
        // Back to beginning.
        index_ = 1;
        current_screen_ = ScreenType::kWelcomeScreen;
      }
      break;
    case ScreenType::kLogScreen:
      if (index_ == 1) {
        if (log_offset_idx_ > 0) {
          --log_offset_idx_;
          UpdateLogArea();
        }
        return;
      } else if (index_ == 2) {
        if (log_offset_idx_ < log_offsets_.size() - 1) {
          ++log_offset_idx_;
          UpdateLogArea();
        }
        return;
      } else if (index_ == 3) {
        // Back to debug options screen.
        index_ = 1;
        current_screen_ = ScreenType::kDebugOptionsScreen;
      }
      break;
  }
  ShowNewScreen();
  return;
}

void Screens::ShowNewScreen() {
  switch (current_screen_) {
    case ScreenType::kWelcomeScreen:
      ShowMiniOsWelcomeScreen();
      break;
    case ScreenType::kNetworkDropDownScreen:
      ShowMiniOsNetworkDropdownScreen();
      break;
    case ScreenType::kExpandedNetworkDropDownScreen:
      ExpandNetworkDropdown();
      break;
    case ScreenType::kPasswordScreen:
      ShowMiniOsGetPasswordScreen();
      break;
    case ScreenType::kLanguageDropDownScreen:
      ShowLanguageDropdown(index_);
      break;
    case ScreenType::kWaitForConnection:
      ShowWaitingForConnectionScreen();
      break;
    case ScreenType::kUserPermissionScreen:
      ShowUserPermissionScreen();
      break;
    case ScreenType::kStartDownload:
      ShowMiniOsDownloadingScreen();
      break;
    case ScreenType::kDownloadError:
      ShowErrorScreen("MiniOS_download_error");
      break;
    case ScreenType::kNetworkError:
      ShowErrorScreen("MiniOS_network_error");
      break;
    case ScreenType::kPasswordError:
      ShowErrorScreen("MiniOS_password_error");
      break;
    case ScreenType::kConnectionError:
      ShowErrorScreen("MiniOS_connection_error");
      break;
    case ScreenType::kGeneralError:
      ShowErrorScreen("MiniOS_general_error");
      break;
    case ScreenType::kDebugOptionsScreen:
      ShowMiniOsDebugOptionsScreen();
      break;
    case ScreenType::kLogScreen:
      ShowMiniOsLogScreen();
      break;
  }
}

void Screens::OnProgressChanged(const update_engine::StatusResult& status) {
  // Only make UI changes when needed to prevent unnecessary screen changes.
  if (!display_update_engine_state_)
    return;

  // Only reshow base screen if moving to a new update stage. This prevents
  // flickering as the screen repaints.
  update_engine::Operation operation = status.current_operation();
  switch (operation) {
    case update_engine::Operation::DOWNLOADING:
      if (previous_update_state_ != operation)
        ShowMiniOsDownloadingScreen();
      ShowProgressPercentage(status.progress());
      break;
    case update_engine::Operation::FINALIZING:
      if (previous_update_state_ != operation) {
        LOG(INFO) << "Finalizing installation please wait.";
      }
      break;
    case update_engine::Operation::UPDATED_NEED_REBOOT:
      ShowMiniOsCompleteScreen();
      // Don't make any more updates to the UI.
      display_update_engine_state_ = false;
      break;
    case update_engine::Operation::REPORTING_ERROR_EVENT:
    case update_engine::Operation::DISABLED:
    case update_engine::Operation::ERROR:
      LOG(ERROR) << "Could not finish the installation, failed with status: "
                 << status.current_operation();
      ChangeToErrorScreen(ScreenType::kDownloadError);
      break;
    default:
      // Only `IDLE` and `CHECKING_FOR_UPDATE` can go back to `IDLE` without
      // any error.
      if (previous_update_state_ != update_engine::Operation::IDLE &&
          previous_update_state_ !=
              update_engine::Operation::CHECKING_FOR_UPDATE &&
          operation == update_engine::Operation::IDLE) {
        LOG(WARNING) << "Update engine went from " << operation
                     << "back to IDLE.";
        ChangeToErrorScreen(ScreenType::kDownloadError);
      }
      break;
  }
  previous_update_state_ = operation;
}

void Screens::OnConnect(const std::string& ssid, brillo::Error* error) {
  if (error) {
    LOG(ERROR) << "Could not connect to " << ssid
               << ". ErrorCode=" << error->GetCode()
               << " ErrorMessage=" << error->GetMessage();
    chosen_network_.clear();
    if (error->GetCode() == shill::kErrorResultInvalidPassphrase) {
      ChangeToErrorScreen(ScreenType::kPasswordError);
    } else {
      // General network error.
      ChangeToErrorScreen(ScreenType::kConnectionError);
    }
    return;
  }
  LOG(INFO) << "Successfully connected to " << ssid;
  index_ = 1;
  current_screen_ = ScreenType::kUserPermissionScreen;
  ShowNewScreen();
}

void Screens::OnUserPermission() {
  if (!recovery_installer_->RepartitionDisk()) {
    LOG(ERROR) << "Could not repartition disk. Unable to continue.";
    ChangeToErrorScreen(ScreenType::kGeneralError);
    return;
  }

  if (!update_engine_proxy_->StartUpdate()) {
    LOG(ERROR) << "Could not start update. Unable to continue.";
    ChangeToErrorScreen(ScreenType::kDownloadError);
    return;
  }

  display_update_engine_state_ = true;
}

void Screens::OnGetNetworks(const std::vector<std::string>& networks,
                            brillo::Error* error) {
  if (error) {
    LOG(ERROR) << "Could not get networks. ErrorCode=" << error->GetCode()
               << " ErrorMessage=" << error->GetMessage();
    network_list_.clear();
    ChangeToErrorScreen(ScreenType::kNetworkError);
    // Add one extra slot for the back button.
    menu_count_[ScreenType::kExpandedNetworkDropDownScreen] = 1;
    return;
  }
  network_list_ = networks;
  LOG(INFO) << "Trying to update network list.";

  // Change the menu count for the Expanded dropdown menu based on number of
  // networks. Add one extra slot for the back button.
  menu_count_[ScreenType::kExpandedNetworkDropDownScreen] =
      network_list_.size() + 1;

  // If already waiting on the dropdown screen, refresh.
  if (current_screen_ == ScreenType::kExpandedNetworkDropDownScreen) {
    index_ = 0;
    ShowNewScreen();
  }
}

void Screens::UpdateNetworkList() {
  network_manager_->GetNetworks();
  chosen_network_.clear();
}

}  // namespace minios
