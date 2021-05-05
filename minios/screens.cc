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

namespace minios {

const char kScreens[] = "etc/screens";

// Colors.
const char kMenuDropdownFrameNavy[] = "0x435066";
const char kMenuDropdownBackgroundBlack[] = "0x2D2E30";

// Key values.
const int kKeyUp = 103;
const int kKeyDown = 108;
const int kKeyEnter = 28;
const int kKeyVolUp = 115;
const int kKeyVolDown = 114;
const int kKeyPower = 116;

// Key state parameters.
const int kFdsMax = 10;
const int kKeyMax = 200;

namespace {
// Buttons Spacing
constexpr int kTitleY = (-1080 / 2) + 238;
constexpr int kBtnYStep = 40;

// Dropdown size.
constexpr int kNetworksPerPage = 10;

constexpr char kLogPath[] = "/var/log/messages";
}  // namespace

bool Screens::Init() {
  CheckDetachable();
  CheckRightToLeft();
  GetVpdRegion();
  ReadHardwareId();

  screens_path_ = root_.Append(kScreens);
  // TODO(vyshu): Change constants.sh and lang_constants.sh to simple text file.
  ReadDimensionConstants();
  if (!ReadLangConstants()) {
    return false;
  }

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

void Screens::ShowLanguageDropdown() {
  constexpr int kItemHeight = 40;
  const int kItemPerPage = (frecon_canvas_size_ - 260) / kItemHeight;

  // Pick begin index such that the selected index is centered on the screen if
  // possible.
  int begin_index =
      std::clamp(index_ - kItemPerPage / 2, 0,
                 static_cast<int>(supported_locales_.size()) - kItemPerPage);

  int offset_y = -frecon_canvas_size_ / 2 + 88;
  const int kBackgroundX = -frecon_canvas_size_ / 2 + 360;
  for (int i = begin_index;
       i < (begin_index + kItemPerPage) && i < supported_locales_.size(); i++) {
    // Get placement for the language image.
    int language_width;
    if (!GetLangConstants(supported_locales_[i], &language_width)) {
      language_width = 95;
      LOG(WARNING) << "Could not get width for " << supported_locales_[i]
                   << ". Defaulting to " << language_width;
    }
    int lang_x = -frecon_canvas_size_ / 2 + language_width / 2 + 40;

    // This is the currently selected language. Show in blue.
    if (index_ == i) {
      ShowBox(kBackgroundX, offset_y, 720, 40, kMenuBlue);
      ShowImage(screens_path_.Append(supported_locales_[i])
                    .Append("language_focused.png"),
                lang_x, offset_y);
    } else {
      ShowBox(kBackgroundX, offset_y, 720, 40, kMenuDropdownFrameNavy);
      ShowBox(kBackgroundX, offset_y, 718, 38, kMenuDropdownBackgroundBlack);
      ShowImage(
          screens_path_.Append(supported_locales_[i]).Append("language.png"),
          lang_x, offset_y);
    }
    offset_y += kItemHeight;
  }
}

void Screens::LanguageMenuOnSelect() {
  ShowLanguageMenu(false);

  // Find index of current locale to show in the dropdown.
  index_ = std::distance(
      supported_locales_.begin(),
      std::find(supported_locales_.begin(), supported_locales_.end(), locale_));
  if (index_ == supported_locales_.size()) {
    // Default to en-US.
    index_ = 9;
    LOG(WARNING) << " Could not find an index to match current locale "
                 << locale_ << ". Defaulting to index " << index_ << " for  "
                 << supported_locales_[index_];
  }

  ShowLanguageDropdown();
}

void Screens::ShowLanguageMenu(bool is_selected) {
  const int kOffsetY = -frecon_canvas_size_ / 2 + 40;
  const int kBgX = -frecon_canvas_size_ / 2 + 145;
  const int kGlobeX = -frecon_canvas_size_ / 2 + 20;
  const int kArrowX = -frecon_canvas_size_ / 2 + 268;
  int language_width;
  if (!GetLangConstants(locale_, &language_width)) {
    language_width = 100;
    LOG(WARNING) << "Could not get language width for " << locale_
                 << ". Defaulting to 100.";
  }
  const int kTextX = -frecon_canvas_size_ / 2 + 40 + language_width / 2;

  base::FilePath menu_background =
      is_selected ? screens_path_.Append("language_menu_bg_focused.png")
                  : screens_path_.Append("language_menu_bg.png");

  ShowImage(menu_background, kBgX, kOffsetY);
  ShowImage(screens_path_.Append("ic_language-globe.png"), kGlobeX, kOffsetY);

  ShowImage(screens_path_.Append("ic_dropdown.png"), kArrowX, kOffsetY);
  ShowMessage("language_folded", kTextX, kOffsetY);
}

void Screens::ShowFooter() {
  constexpr int kQrCodeSize = 86;
  const int kQrCodeX = (-frecon_canvas_size_ / 2) + (kQrCodeSize / 2);
  const int kQrCodeY = (frecon_canvas_size_ / 2) - (kQrCodeSize / 2) - 56;

  const int kSeparatorX = 410 - (frecon_canvas_size_ / 2);
  const int kSeparatorY = kQrCodeY;
  constexpr int kFooterLineHeight = 18;

  const int kFooterY = (frecon_canvas_size_ / 2) - kQrCodeSize + 9 - 56;
  const int kFooterLeftX =
      kQrCodeX + (kQrCodeSize / 2) + 16 + (kDefaultMessageWidth / 2);
  const int kFooterRightX = kSeparatorX + 32 + (kDefaultMessageWidth / 2);

  ShowMessage("footer_left_1", kFooterLeftX, kFooterY);
  ShowMessage("footer_left_2", kFooterLeftX,
              kFooterY + kFooterLineHeight * 2 + 14);
  ShowMessage("footer_left_3", kFooterLeftX,
              kFooterY + kFooterLineHeight * 3 + 14);

  constexpr int kNavButtonHeight = 24;
  const int kNavButtonY =
      (frecon_canvas_size_ / 2) - (kNavButtonHeight / 2) - 56;
  int nav_btn_x = kSeparatorX + 32;
  // Navigation key icons.
  const std::string kFooterType = is_detachable_ ? "tablet" : "clamshell";
  const std::string kNavKeyEnter =
      is_detachable_ ? "button_power" : "key_enter";
  const std::string kNavKeyUp = is_detachable_ ? "button_volume_up" : "key_up";
  const std::string kNavKeyDown =
      is_detachable_ ? "button_volume_down" : "key_down";

  constexpr int kUpDownIconWidth = 24;
  constexpr int kIconPadding = 8;
  const int kEnterIconWidth = is_detachable_ ? 40 : 66;

  ShowMessage("footer_right_1_" + kFooterType, kFooterRightX, kFooterY);
  ShowMessage("footer_right_2_" + kFooterType, kFooterRightX,
              kFooterY + kFooterLineHeight + 8);

  nav_btn_x += kEnterIconWidth / 2;
  ShowImage(screens_path_.Append("nav-" + kNavKeyEnter + ".png"), nav_btn_x,
            kNavButtonY);
  nav_btn_x += kEnterIconWidth / 2 + kIconPadding + kUpDownIconWidth / 2;
  ShowImage(screens_path_.Append("nav-" + kNavKeyUp + ".png"), nav_btn_x,
            kNavButtonY);
  nav_btn_x += kIconPadding + kUpDownIconWidth;
  ShowImage(screens_path_.Append("nav-" + kNavKeyDown + ".png"), nav_btn_x,
            kNavButtonY);

  ShowImage(screens_path_.Append("qr_code.png"), kQrCodeX, kQrCodeY);
  int hwid_len = hwid_.size();
  int hwid_x = kQrCodeX + (kQrCodeSize / 2) + 16 + 5;
  const int kHwidY = kFooterY + kFooterLineHeight;

  if (right_to_left_) {
    hwid_x = -hwid_x - kMonospaceGlyphWidth * (hwid_len - 2);
  }

  ShowText(hwid_, hwid_x, kHwidY, "grey");
  ShowBox(kSeparatorX, kSeparatorY, 1, kQrCodeSize, kMenuGrey);
}

void Screens::MessageBaseScreen() {
  ClearMainArea();
  ShowLanguageMenu(false);
  ShowFooter();
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
  std::string keyboard_layout;
  if (!MapRegionToKeyboard(&keyboard_layout)) {
    LOG(WARNING)
        << "Could not find xkb layout for given region. Defaulting to US.";
    keyboard_layout = "us";
  }
  KeyReader password_key_reader =
      KeyReader(/*include_usb=*/true, keyboard_layout);
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

bool Screens::ReadLangConstants() {
  lang_constants_.clear();
  supported_locales_.clear();
  // Read language widths from lang_constants.sh into memory.
  auto lang_constants_path = screens_path_.Append("lang_constants.sh");
  if (!base::PathExists(lang_constants_path)) {
    LOG(ERROR) << "Language constants path: " << lang_constants_path
               << " not found.";
    return false;
  }

  std::string const_values;
  if (!ReadFileToString(lang_constants_path, &const_values)) {
    LOG(ERROR) << "Could not read lang constants file " << lang_constants_path;
    return false;
  }

  if (!base::SplitStringIntoKeyValuePairs(const_values, '=', '\n',
                                          &lang_constants_)) {
    LOG(ERROR) << "Unable to parse language width information.";
    return false;
  }
  for (const auto& pair : lang_constants_) {
    if (pair.first == "SUPPORTED_LOCALES") {
      // Parse list of supported locales and store separately.
      std::string locale_list;
      if (!base::RemoveChars(pair.second, "\"", &locale_list))
        LOG(WARNING) << "Unable to remove surrounding quotes from locale list.";
      supported_locales_ = base::SplitString(
          locale_list, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    }
  }

  // Add size of language dropdown menu using the number of locales.
  menu_count_[ScreenType::kLanguageDropDownScreen] = supported_locales_.size();

  if (supported_locales_.empty()) {
    LOG(ERROR) << "Unable to get supported locales. Will not be able to "
                  "change locale.";
    return false;
  }
  return true;
}

bool Screens::GetLangConstants(const std::string& locale, int* lang_width) {
  if (lang_constants_.empty()) {
    LOG(ERROR) << "No language widths available.";
    return false;
  }

  // Lang_consts uses '_' while supported locale list uses '-'.
  std::string token;
  base::ReplaceChars(locale, "-", "_", &token);
  token = "LANGUAGE_" + token + "_WIDTH";

  // Find the width for the token.
  for (const auto& width_token : lang_constants_) {
    if (width_token.first == token) {
      if (!base::StringToInt(width_token.second, lang_width)) {
        LOG(ERROR) << "Could not convert " << width_token.second
                   << " to a number.";
        return false;
      }
      return true;
    }
  }
  return false;
}

void Screens::OnLocaleChange() {
  // Change locale and update constants.
  locale_ = supported_locales_[index_];
  CheckRightToLeft();
  ReadDimensionConstants();
  ClearScreen();
  ShowFooter();
  // Reset index state to go back to the MiniOs flow.
  index_ = 1;
}

void Screens::ShowCollapsedNetworkDropDown(bool is_selected) {
  const int kOffsetY = -frecon_canvas_size_ / 2 + 350;
  const int kBgX = -frecon_canvas_size_ / 2 + 145;
  const int kGlobeX = -frecon_canvas_size_ / 2 + 20;
  const int kArrowX = -frecon_canvas_size_ / 2 + 268;
  const int kTextX = -frecon_canvas_size_ / 2 + 100;

  // Currently using language and globe icons as placeholders.
  base::FilePath menu_background =
      is_selected ? screens_path_.Append("language_menu_bg_focused.png")
                  : screens_path_.Append("language_menu_bg.png");

  ShowImage(menu_background, kBgX, kOffsetY);
  ShowImage(screens_path_.Append("ic_language-globe.png"), kGlobeX, kOffsetY);
  ShowImage(screens_path_.Append("ic_dropdown.png"), kArrowX, kOffsetY);
  ShowMessage("btn_MiniOS_display_options", kTextX, kOffsetY);
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

void Screens::CheckRightToLeft() {
  // TODO(vyshu): Create an unblocked_terms.txt to allow "he" for Hebrew.
  right_to_left_ = (locale_ == "ar" || locale_ == "fa" || locale_ == "he");
}

void Screens::CheckDetachable() {
  is_detachable_ =
      base::PathExists(root_.Append("etc/cros-initramfs/is_detachable"));
}

void Screens::GetVpdRegion() {
  if (ReadFileToString(root_.Append("sys/firmware/vpd/ro/region"),
                       &vpd_region_)) {
    return;
  }
  LOG(WARNING) << "Could not read vpd region from file. Trying commandline.";
  int exit_code = 0;
  std::string error;
  if (!process_manager_->RunCommandWithOutput(
          {"/bin/vpd", "-g", "region"}, &exit_code, &vpd_region_, &error) ||
      exit_code) {
    vpd_region_ = "us";
    PLOG(WARNING) << "Error getting vpd -g region. Exit code " << exit_code
                  << " with error " << error << ". Defaulting to 'us'. ";
    return;
  }
  return;
}

void Screens::ReadHardwareId() {
  int exit_code = 0;
  std::string output, error;
  if (!process_manager_->RunCommandWithOutput({"/bin/crossystem", "hwid"},
                                              &exit_code, &output, &error) ||
      exit_code) {
    hwid_ = "CHROMEBOOK";
    PLOG(WARNING)
        << "Could not get hwid from crossystem. Exited with exit code "
        << exit_code << " and error " << error
        << ". Defaulting to 'CHROMEBOOK'.";
    return;
  }

  // Truncate HWID.
  std::vector<std::string> hwid_parts = base::SplitString(
      output, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  hwid_ = hwid_parts[0];
  return;
}

bool Screens::MapRegionToKeyboard(std::string* xkb_keyboard_layout) {
  std::string cros_region_json;
  if (!ReadFileToString(root_.Append("usr/share/misc/cros-regions.json"),
                        &cros_region_json)) {
    PLOG(ERROR) << "Could not read JSON mapping from cros-regions.json.";
    return false;
  }

  base::JSONReader::ValueWithError json_output =
      base::JSONReader::ReadAndReturnValueWithError(cros_region_json);
  if (!json_output.value || !json_output.value->is_dict()) {
    LOG(ERROR) << "Could not read json. " << json_output.error_message;
    return false;
  }

  // Look up mapping between vpd region and xkb keyboard layout.
  const base::Value* kRegionInfo = json_output.value->FindDictKey(vpd_region_);
  if (!kRegionInfo) {
    LOG(ERROR) << "Region " << vpd_region_ << " not found.";
    return false;
  }

  const base::Value* kKeyboard = kRegionInfo->FindListKey("keyboards");
  if (!kKeyboard || kKeyboard->GetList().empty()) {
    LOG(ERROR) << "Could not retrieve keyboards for given region "
               << vpd_region_
               << ". Available region information: " << *kRegionInfo;
    return false;
  }

  // Always use the first keyboard in the list.
  std::vector<std::string> keyboard_parts =
      base::SplitString(kKeyboard->GetList()[0].GetString(), ":",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (keyboard_parts.size() < 2) {
    LOG(ERROR) << "Could not parse keyboard information for region  "
               << vpd_region_;
    return false;
  }
  *xkb_keyboard_layout = keyboard_parts[1];
  return true;
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
        OnLocaleChange();
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
      ShowLanguageDropdown();
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
