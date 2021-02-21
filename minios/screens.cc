// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens.h"

#include <base/json/json_reader.h>
#include <base/strings/string_number_conversions.h>
#include <base/values.h>

namespace screens {

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
constexpr int kCanvasSize = 1080;

// Buttons Spacing
constexpr int kTitleY = (-kCanvasSize / 2) + 238;
constexpr int kBtnYStep = 40;
}  // namespace

bool Screens::Init() {
  CheckDetachable();
  CheckRightToLeft();
  GetVpdRegion();
  ReadHardwareId();

  screens_path_ = root_.Append(kScreens);
  // TODO(vyshu): Change constants.sh and lang_constants.sh to simple text file.
  ReadDimensionConstants();
  ReadLangConstants();

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
  constexpr int kItemPerPage = (kCanvasSize - 260) / kItemHeight;

  // Pick begin index such that the selected index is centered on the screen if
  // possible.
  int begin_index =
      std::clamp(index_ - kItemPerPage / 2, 0,
                 static_cast<int>(supported_locales_.size()) - kItemPerPage);

  int offset_y = -kCanvasSize / 2 + 88;
  constexpr int kBackgroundX = -kCanvasSize / 2 + 360;
  for (int i = begin_index;
       i < (begin_index + kItemPerPage) && i < supported_locales_.size(); i++) {
    // Get placement for the language image.
    int language_width;
    if (!GetLangConstants(supported_locales_[i], &language_width)) {
      language_width = 95;
      LOG(WARNING) << "Could not get width for " << supported_locales_[i]
                   << ". Defaulting to " << language_width;
    }
    int lang_x = -kCanvasSize / 2 + language_width / 2 + 40;

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
  constexpr int kOffsetY = -kCanvasSize / 2 + 40;
  constexpr int kBgX = -kCanvasSize / 2 + 145;
  constexpr int kGlobeX = -kCanvasSize / 2 + 20;
  constexpr int kArrowX = -kCanvasSize / 2 + 268;
  int language_width;
  if (!GetLangConstants(locale_, &language_width)) {
    language_width = 100;
    LOG(WARNING) << "Could not get language width for " << locale_
                 << ". Defaulting to 100.";
  }
  const int kTextX = -kCanvasSize / 2 + 40 + language_width / 2;

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
  constexpr int kQrCodeX = (-kCanvasSize / 2) + (kQrCodeSize / 2);
  constexpr int kQrCodeY = (kCanvasSize / 2) - (kQrCodeSize / 2) - 56;

  constexpr int kSeparatorX = 410 - (kCanvasSize / 2);
  constexpr int kSeparatorY = kQrCodeY;
  constexpr int kFooterLineHeight = 18;

  constexpr int kFooterY = (kCanvasSize / 2) - kQrCodeSize + 9 - 56;
  const int kFooterLeftX =
      kQrCodeX + (kQrCodeSize / 2) + 16 + (kDefaultMessageWidth / 2);
  const int kFooterRightX = kSeparatorX + 32 + (kDefaultMessageWidth / 2);

  ShowMessage("footer_left_1", kFooterLeftX, kFooterY);
  ShowMessage("footer_left_2", kFooterLeftX,
              kFooterY + kFooterLineHeight * 2 + 14);
  ShowMessage("footer_left_3", kFooterLeftX,
              kFooterY + kFooterLineHeight * 3 + 14);

  constexpr int kNavButtonHeight = 24;
  constexpr int kNavButtonY = (kCanvasSize / 2) - (kNavButtonHeight / 2) - 56;
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

void Screens::ShowMiniOsDropdownScreen() {
  MessageBaseScreen();
  ShowInstructions("title_MiniOS_dropdown");
  ShowStepper({"1-done", "2", "3"});
  ShowLanguageMenu(index_ == 0);
  ShowCollapsedItemMenu((index_ == 1));
  ShowButton("btn_back", kTitleY + 58 + (4 * kBtnYStep), (index_ == 2),
             default_button_width_, false);
}

void Screens::ExpandItemDropdown() {
  SetItems();
  ShowLanguageMenu(false);
  ShowCollapsedItemMenu(true);
  ShowItemDropdown();
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
  key_reader::KeyReader password_key_reader =
      key_reader::KeyReader(/*include_usb=*/true, keyboard_layout);
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
}

void Screens::ShowMiniOsDownloadingScreen() {
  MessageBaseScreen();
  ShowInstructionsWithTitle("MiniOS_downloading");
  ShowStepper({"done", "done", "3-done"});
  ShowLanguageMenu(false);
  ShowProgressBar(10);
  ShowMiniOsCompleteScreen();
}

void Screens::ShowMiniOsCompleteScreen() {
  MessageBaseScreen();
  ShowInstructions("title_MiniOS_complete");
  ShowStepper({"done", "done", "done"});
  ShowLanguageMenu(false);
  ShowProgressBar(5);
  // TODO(vyshu): Automatically reboot after timeout or on button selection.
  ShowButton("Reboot", -100, false, default_button_width_, true);
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

void Screens::ReadLangConstants() {
  lang_constants_.clear();
  supported_locales_.clear();
  // Read language widths from lang_constants.sh into memory.
  auto lang_constants_path = screens_path_.Append("lang_constants.sh");
  std::string const_values;
  if (!ReadFileToString(lang_constants_path, &const_values)) {
    LOG(ERROR) << "Could not read lang constants file " << lang_constants_path;
    return;
  }

  if (!base::SplitStringIntoKeyValuePairs(const_values, '=', '\n',
                                          &lang_constants_)) {
    LOG(ERROR) << "Unable to parse language width information.";
    return;
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
  menu_count_[static_cast<int>(ScreenType::kLanguageDropDownScreen)] =
      supported_locales_.size();

  if (supported_locales_.empty()) {
    LOG(WARNING) << "Unable to get supported locales. Will not be able to "
                    "change locale.";
  }
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

void Screens::ShowCollapsedItemMenu(bool is_selected) {
  constexpr int kOffsetY = -kCanvasSize / 2 + 350;
  constexpr int kBgX = -kCanvasSize / 2 + 145;
  constexpr int kGlobeX = -kCanvasSize / 2 + 20;
  constexpr int kArrowX = -kCanvasSize / 2 + 268;
  constexpr int kTextX = -kCanvasSize / 2 + 100;

  // Currently using language and globe icons as placeholders.
  base::FilePath menu_background =
      is_selected ? screens_path_.Append("language_menu_bg_focused.png")
                  : screens_path_.Append("language_menu_bg.png");

  ShowImage(menu_background, kBgX, kOffsetY);
  ShowImage(screens_path_.Append("ic_language-globe.png"), kGlobeX, kOffsetY);
  ShowImage(screens_path_.Append("ic_dropdown.png"), kArrowX, kOffsetY);
  ShowMessage("btn_MiniOS_display_options", kTextX, kOffsetY);
}

void Screens::ShowItemDropdown() {
  constexpr int kItemPerPage = 10;
  // Pick begin index such that the selected index is centered on the screen.
  int begin_index =
      std::clamp(index_ - kItemPerPage / 2, 0,
                 static_cast<int>(supported_locales_.size()) - kItemPerPage);

  int offset_y = -kCanvasSize / 2 + 350 + 40;
  constexpr int kBackgroundX = -kCanvasSize / 2 + 360;
  constexpr int kOffsetX = -kCanvasSize / 2 + 60;
  constexpr int kItemHeight = 40;
  for (int i = begin_index;
       i < (begin_index + kItemPerPage) && i < item_list_.size(); i++) {
    if (index_ == i) {
      ShowBox(kBackgroundX, offset_y, 720, 40, kMenuBlue);
      ShowText(item_list_[i], kOffsetX, offset_y, "black");
    } else {
      ShowBox(kBackgroundX, offset_y, 720, 40, kMenuDropdownFrameNavy);
      ShowBox(kBackgroundX, offset_y, 718, 38, kMenuDropdownBackgroundBlack);
      ShowText(item_list_[i], kOffsetX, offset_y, "grey");
    }
    offset_y += kItemHeight;
  }
}

void Screens::SetItems() {
  // TODO(vyshu): temporary item names, replace with shill information.
  item_list_ = {" item 1", "item2_public", "testing ! 1 2 ",
                "32_char_is_the_longest_item_name"};
  // Change the menu count for the Expanded dropdown menu based on number of
  // items.
  menu_count_[static_cast<int>(ScreenType::kExpandedDropDownScreen)] =
      item_list_.size();
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
    UpdateButtons(menu_count_[static_cast<int>(current_screen_)], key_changed,
                  &enter);
    SwitchScreen(enter);
    return;
  } else if (!key_released) {
    key_states_[fd_index][key_changed] = true;
  }
}

void Screens::SwitchScreen(bool enter) {
  if (enter && index_ == 0 &&
      current_screen_ != ScreenType::kLanguageDropDownScreen &&
      current_screen_ != ScreenType::kExpandedDropDownScreen &&
      current_screen_ != ScreenType::kDoneWithFlow) {
    previous_screen_ = current_screen_;
    current_screen_ = ScreenType::kLanguageDropDownScreen;
    LanguageMenuOnSelect();
    return;
  }

  if (!enter) {
    ShowNewScreen();
    return;
  }

  switch (current_screen_) {
    case ScreenType::kWelcomeScreen:
      if (index_ == 1) {
        current_screen_ = ScreenType::kDropDownScreen;
      }
      index_ = 1;
      break;
    case ScreenType::kDropDownScreen:
      if (index_ == 1) {
        index_ = 0;
        current_screen_ = ScreenType::kExpandedDropDownScreen;
      } else {
        index_ = 1;
        current_screen_ = ScreenType::kWelcomeScreen;
      }
      break;
    case ScreenType::kExpandedDropDownScreen:
      index_ = 1;
      current_screen_ = ScreenType::kPasswordScreen;
      break;
    case ScreenType::kPasswordScreen:
      if (index_ == 1) {
        GetPassword();
        current_screen_ = ScreenType::kDoneWithFlow;
      } else {
        index_ = 1;
        current_screen_ = ScreenType::kDropDownScreen;
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
    case ScreenType::kDoneWithFlow:
      return;
  }
  ShowNewScreen();
  return;
}

void Screens::ShowNewScreen() {
  switch (current_screen_) {
    case ScreenType::kWelcomeScreen:
      ShowMiniOsWelcomeScreen();
      break;
    case ScreenType::kDropDownScreen:
      ShowMiniOsDropdownScreen();
      break;
    case ScreenType::kExpandedDropDownScreen:
      ExpandItemDropdown();
      break;
    case ScreenType::kPasswordScreen:
      ShowMiniOsGetPasswordScreen();
      break;
    case ScreenType::kLanguageDropDownScreen:
      ShowLanguageDropdown();
      break;
    case ScreenType::kDoneWithFlow:
      ShowMiniOsDownloadingScreen();
  }
}

}  // namespace screens
