// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens.h"

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <utility>

#include <base/files/file_util.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <base/values.h>

namespace screens {

const char kScreens[] = "etc/screens";

// Colors.
const char kMenuBlack[] = "0x202124";
const char kMenuBlue[] = "0x8AB4F8";
const char kMenuGrey[] = "0x3F4042";
const char kMenuButtonFrameGrey[] = "0x9AA0A6";
const char kMenuDropdownFrameNavy[] = "0x435066";
const char kMenuDropdownBackgroundBlack[] = "0x2D2E30";

// Key values.
const int kKeyUp = 103;
const int kKeyDown = 108;
const int kKeyEnter = 28;
const int kKeyVolUp = 115;
const int kKeyVolDown = 114;
const int kKeyPower = 116;

namespace {
constexpr char kConsole0[] = "dev/pts/0";

// Dimensions.
// TODO(vyshu): Get this from frecon.
constexpr int kFreconScalingFactor = 1;
// TODO(vyshu): Get this from frecon print-resolution.
constexpr int kCanvasSize = 1080;
constexpr int kMonospaceGlyphHeight = 20;
constexpr int kMonospaceGlyphWidth = 10;
constexpr int kDefaultMessageWidth = 720;
constexpr int kButtonHeight = 32;
constexpr int kButtonMargin = 8;
constexpr int kDefaultButtonWidth = 80;

constexpr int kNewLineChar = 10;

// Buttons
constexpr int kTitleY = (-kCanvasSize / 2) + 238;
constexpr int kBtnYStep = kButtonHeight + kButtonMargin;
constexpr char kButtonWidthToken[] = "DEBUG_OPTIONS_BTN_WIDTH";
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
  return true;
}

bool Screens::InitForTest() {
  screens_path_ = root_.Append(kScreens);
  ReadDimensionConstants();
  return true;
}

bool Screens::ShowText(const std::string& text,
                       int glyph_offset_h,
                       int glyph_offset_v,
                       const std::string& color) {
  base::FilePath glyph_dir = screens_path_.Append("glyphs").Append(color);
  const int kTextStart = glyph_offset_h;

  for (const auto& chr : text) {
    int char_num = static_cast<int>(chr);
    base::FilePath chr_file_path =
        glyph_dir.Append(base::NumberToString(char_num) + ".png");
    if (char_num == kNewLineChar) {
      glyph_offset_v += kMonospaceGlyphHeight;
      glyph_offset_h = kTextStart;
    } else {
      int offset_rtl = right_to_left_ ? -glyph_offset_h : glyph_offset_h;
      if (!ShowImage(chr_file_path, offset_rtl, glyph_offset_v)) {
        LOG(ERROR) << "Failed to show image " << chr_file_path << " for text "
                   << text;
        return false;
      }
      glyph_offset_h += kMonospaceGlyphWidth;
    }
  }
  return true;
}

bool Screens::ShowImage(const base::FilePath& image_name,
                        int offset_x,
                        int offset_y) {
  if (right_to_left_)
    offset_x = -offset_x;
  std::string command = base::StringPrintf(
      "\033]image:file=%s;offset=%d,%d;scale=%d\a", image_name.value().c_str(),
      offset_x, offset_y, kFreconScalingFactor);
  if (!base::AppendToFile(base::FilePath(root_).Append(kConsole0),
                          command.c_str(), command.size())) {
    LOG(ERROR) << "Could not write " << image_name << "  to console.";
    return false;
  }

  return true;
}

bool Screens::ShowBox(int offset_x,
                      int offset_y,
                      int size_x,
                      int size_y,
                      const std::string& color) {
  size_x = std::max(size_x, 1);
  size_y = std::max(size_y, 1);
  if (right_to_left_)
    offset_x = -offset_x;

  std::string command = base::StringPrintf(
      "\033]box:color=%s;size=%d,%d;offset=%d,%d;scale=%d\a", color.c_str(),
      size_x, size_y, offset_x, offset_y, kFreconScalingFactor);

  if (!base::AppendToFile(base::FilePath(root_).Append(kConsole0),
                          command.c_str(), command.size())) {
    LOG(ERROR) << "Could not write show box command to console.";
    return false;
  }

  return true;
}

bool Screens::ShowMessage(const std::string& message_token,
                          int offset_x,
                          int offset_y) {
  // Determine the filename of the message resource. Fall back to en-US if
  // the localized version of the message is not available.
  base::FilePath message_file_path =
      screens_path_.Append(locale_).Append(message_token + ".png");
  if (!base::PathExists(message_file_path)) {
    if (locale_ == "en-US") {
      LOG(ERROR) << "Message " << message_token
                 << " not found in en-US. No fallback available.";
      return false;
    }
    LOG(WARNING) << "Could not find " << message_token << " in " << locale_
                 << " trying default locale en-US.";
    message_file_path =
        screens_path_.Append("en-US").Append(message_token + ".png");
    if (!base::PathExists(message_file_path)) {
      LOG(ERROR) << "Message " << message_token << " not found in path "
                 << message_file_path;
      return false;
    }
  }
  return ShowImage(message_file_path, offset_x, offset_y);
}

void Screens::ShowInstructions(const std::string& message_token) {
  constexpr int kXOffset = (-kCanvasSize / 2) + (kDefaultMessageWidth / 2);
  constexpr int kYOffset = (-kCanvasSize / 2) + 283;
  if (!ShowMessage(message_token, kXOffset, kYOffset))
    LOG(WARNING) << "Unable to show " << message_token;
}

void Screens::ShowInstructionsWithTitle(const std::string& message_token) {
  constexpr int kXOffset = (-kCanvasSize / 2) + (kDefaultMessageWidth / 2);

  int title_height;
  if (!GetDimension("TITLE_" + message_token + "_HEIGHT", &title_height)) {
    title_height = 40;
    LOG(WARNING) << "Unable to get title constant for  " << message_token
                 << ". Defaulting to " << title_height;
  }
  int desc_height;
  if (!GetDimension("DESC_" + message_token + "_HEIGHT", &desc_height)) {
    desc_height = 40;
    LOG(WARNING) << "Unable to get description constant for  " << message_token
                 << ". Defaulting to " << desc_height;
  }

  const int title_y = (-kCanvasSize / 2) + 220 + (title_height / 2);
  const int desc_y = title_y + (title_height / 2) + 16 + (desc_height / 2);
  if (!ShowMessage("title_" + message_token, kXOffset, title_y))
    LOG(WARNING) << "Unable to show title " << message_token;
  if (!ShowMessage("desc_" + message_token, kXOffset, desc_y))
    LOG(WARNING) << "Unable to show description " << message_token;
}

void Screens::ShowProgressBar(double seconds) {
  constexpr int kProgressIncrement = 10;
  constexpr int kProgressHeight = 4;

  ShowBox(0, 0, kProgressIncrement * 100, kProgressHeight, kMenuGrey);

  constexpr int kLeftIncrement = -500;
  int leftmost = kLeftIncrement;

  // Can be increased for a smoother progress bar.
  constexpr int kUpdatesPerSecond = 10;
  const double percent_update = 100 / (seconds * kUpdatesPerSecond);
  double current_percent = 0;

  while (current_percent < 100) {
    current_percent += percent_update;
    int rightmost = kLeftIncrement + kProgressIncrement * current_percent;
    while (leftmost < rightmost) {
      ShowBox(leftmost + kProgressIncrement / 2, 0, kProgressIncrement + 2,
              kProgressHeight, kMenuBlue);
      leftmost += kProgressIncrement;
    }
    base::PlatformThread::Sleep(
        base::TimeDelta::FromMilliseconds(1000 / kUpdatesPerSecond));
  }
}

void Screens::ClearMainArea() {
  constexpr int kFooterHeight = 142;
  if (!ShowBox(0, -kFooterHeight / 2, kCanvasSize + 100,
               (kCanvasSize - kFooterHeight), kMenuBlack))
    LOG(WARNING) << "Could not clear main area.";
}

void Screens::ClearScreen() {
  if (!ShowBox(0, 0, kCanvasSize + 100, kCanvasSize, kMenuBlack))
    LOG(WARNING) << "Could not clear screen.";
}

void Screens::WaitMenuInput(int menu_count, int* index, bool* enter) {
  std::vector<int> wait_keys;
  if (!is_detachable_)
    wait_keys = {kKeyUp, kKeyDown, kKeyEnter};
  else
    wait_keys = {kKeyVolDown, kKeyVolUp, kKeyPower};
  int key;
  while (!key_reader_.EvWaitForKeys(wait_keys, &key)) {
    LOG(WARNING) << "Error while waiting for keys, trying again.";
    // Sleep and try again.
    base::PlatformThread::Sleep(base::TimeDelta::FromSeconds(1));
  }
  return UpdateButtons(menu_count, key, index, enter);
}

void Screens::ShowButton(const std::string& message_token,
                         int offset_y,
                         bool is_selected,
                         int inner_width,
                         bool is_text) {
  const int btn_padding = 32;  // Left and right padding.
  int left_padding_x = (-kCanvasSize / 2) + (btn_padding / 2);
  const int offset_x = left_padding_x + (btn_padding / 2) + (inner_width / 2);
  int right_padding_x = offset_x + (btn_padding / 2) + (inner_width / 2);
  // Clear previous state.
  if (!ShowBox(offset_x, offset_y, (btn_padding * 2 + inner_width),
               kButtonHeight, kMenuBlack)) {
    LOG(WARNING) << "Could not clear button area.";
  }

  if (right_to_left_) {
    std::swap(left_padding_x, right_padding_x);
  }

  if (is_selected) {
    ShowImage(screens_path_.Append("btn_bg_left_focused.png"), left_padding_x,
              offset_y);
    ShowImage(screens_path_.Append("btn_bg_right_focused.png"), right_padding_x,
              offset_y);

    ShowBox(offset_x, offset_y, inner_width, kButtonHeight, kMenuBlue);
    if (is_text) {
      ShowText(message_token, left_padding_x, offset_y, "black");
    } else {
      ShowMessage(message_token + "_focused", offset_x, offset_y);
    }
  } else {
    ShowImage(screens_path_.Append("btn_bg_left.png"), left_padding_x,
              offset_y);
    ShowImage(screens_path_.Append("btn_bg_right.png"), right_padding_x,
              offset_y);
    ShowBox(offset_x, offset_y - (kButtonHeight / 2) + 1, inner_width, 1,
            kMenuButtonFrameGrey);
    ShowBox(offset_x, offset_y + (kButtonHeight / 2), inner_width, 1,
            kMenuButtonFrameGrey);
    if (is_text) {
      ShowText(message_token, left_padding_x, offset_y, "white");
    } else {
      ShowMessage(message_token, offset_x, offset_y);
    }
  }
}

void Screens::ShowStepper(const std::vector<std::string>& steps) {
  // The icon real size is 24x24, but it occupies a 36x36 block. Use 36 here for
  // simplicity.
  constexpr int kIconSize = 36;
  constexpr int kSeparatorLength = 46;
  constexpr int kPadding = 6;

  int stepper_x = (-kCanvasSize / 2) + (kIconSize / 2);
  constexpr int kStepperXStep = kIconSize + kSeparatorLength + (kPadding * 2);
  constexpr int kStepperY = 144 - (kCanvasSize / 2);
  int separator_x =
      (-kCanvasSize / 2) + kIconSize + kPadding + (kSeparatorLength / 2);

  for (const auto& step : steps) {
    base::FilePath stepper_image = screens_path_.Append("ic_" + step + ".png");
    if (!base::PathExists(stepper_image)) {
      // TODO(vyshu): Create a new generic icon to be used instead of done.
      LOG(WARNING) << "Stepper icon " << stepper_image
                   << " not found. Defaulting to the done icon.";
      stepper_image = screens_path_.Append("ic_done.png");
      if (!base::PathExists(stepper_image)) {
        LOG(ERROR) << "Could not find stepper icon done. Cannot show stepper.";
        return;
      }
    }
    ShowImage(stepper_image, stepper_x, kStepperY);
    stepper_x += kStepperXStep;
  }

  for (int i = 0; i < steps.size() - 1; ++i) {
    ShowBox(separator_x, kStepperY, kSeparatorLength, 1, kMenuGrey);
    separator_x += kStepperXStep;
  }
}

void Screens::ShowLanguageDropdown(int index) {
  constexpr int kItemHeight = 40;
  constexpr int kItemPerPage = (kCanvasSize - 260) / kItemHeight;

  // Pick begin index such that the selected index is centered on the screen if
  // possible.
  int begin_index =
      std::clamp(index - kItemPerPage / 2, 0,
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
    if (index == i) {
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
  int index = std::distance(
      supported_locales_.begin(),
      std::find(supported_locales_.begin(), supported_locales_.end(), locale_));
  if (index == supported_locales_.size()) {
    // Default to en-US.
    index = 9;
    LOG(WARNING) << " Could not find an index to match current locale "
                 << locale_ << ". Defaulting to index " << index << " for  "
                 << supported_locales_[index];
  }

  ShowLanguageDropdown(index);

  bool enter = false;
  while (true) {
    WaitMenuInput(supported_locales_.size(), &index, &enter);
    if (enter && index >= 0) {
      // Selected a new locale. Update the constants and whether it is read from
      // rtl.
      locale_ = supported_locales_[index];
      CheckRightToLeft();
      ReadDimensionConstants();
      ClearScreen();
      ShowFooter();
      LOG(INFO) << "Changed selected locale to " << supported_locales_[index];
      return;
    }
    // Update drop down menu with new highlighted selection.
    ShowLanguageDropdown(index);
  }
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
  const int text_x = -kCanvasSize / 2 + 40 + language_width / 2;

  base::FilePath menu_background =
      is_selected ? screens_path_.Append("language_menu_bg_focused.png")
                  : screens_path_.Append("language_menu_bg.png");

  ShowImage(menu_background, kBgX, kOffsetY);
  ShowImage(screens_path_.Append("ic_language-globe.png"), kGlobeX, kOffsetY);

  ShowImage(screens_path_.Append("ic_dropdown.png"), kArrowX, kOffsetY);
  ShowMessage("language_folded", text_x, kOffsetY);
}

void Screens::ShowFooter() {
  constexpr int kQrCodeSize = 86;
  constexpr int kQrCodeX = (-kCanvasSize / 2) + (kQrCodeSize / 2);
  constexpr int kQrCodeY = (kCanvasSize / 2) - (kQrCodeSize / 2) - 56;

  constexpr int kSeparatorX = 410 - (kCanvasSize / 2);
  constexpr int kSeparatorY = kQrCodeY;
  constexpr int kFooterLineHeight = 18;

  constexpr int kFooterY = (kCanvasSize / 2) - kQrCodeSize + 9 - 56;
  constexpr int kFooterLeftX =
      kQrCodeX + (kQrCodeSize / 2) + 16 + (kDefaultMessageWidth / 2);
  constexpr int kFooterRightX = kSeparatorX + 32 + (kDefaultMessageWidth / 2);

  ShowMessage("footer_left_1", kFooterLeftX, kFooterY);
  ShowMessage("footer_left_2", kFooterLeftX,
              kFooterY + kFooterLineHeight * 2 + 14);
  ShowMessage("footer_left_3", kFooterLeftX,
              kFooterY + kFooterLineHeight * 3 + 14);

  constexpr int kNavButtonHeight = 24;
  constexpr int kNavButtonY = (kCanvasSize / 2) - (kNavButtonHeight / 2) - 56;
  int nav_btn_x = kSeparatorX + 32;
  // Navigation key icons.
  const std::string footer_type = is_detachable_ ? "tablet" : "clamshell";
  const std::string nav_key_enter =
      is_detachable_ ? "button_power" : "key_enter";
  const std::string nav_key_up = is_detachable_ ? "button_volume_up" : "key_up";
  const std::string nav_key_down =
      is_detachable_ ? "button_volume_down" : "key_down";

  constexpr int kUpDownIconWidth = 24;
  constexpr int kIconPadding = 8;
  const int enter_icon_width = is_detachable_ ? 40 : 66;

  ShowMessage("footer_right_1_" + footer_type, kFooterRightX, kFooterY);
  ShowMessage("footer_right_2_" + footer_type, kFooterRightX,
              kFooterY + kFooterLineHeight + 8);

  nav_btn_x += enter_icon_width / 2;
  ShowImage(screens_path_.Append("nav-" + nav_key_enter + ".png"), nav_btn_x,
            kNavButtonY);
  nav_btn_x += enter_icon_width / 2 + kIconPadding + kUpDownIconWidth / 2;
  ShowImage(screens_path_.Append("nav-" + nav_key_up + ".png"), nav_btn_x,
            kNavButtonY);
  nav_btn_x += kIconPadding + kUpDownIconWidth;
  ShowImage(screens_path_.Append("nav-" + nav_key_down + ".png"), nav_btn_x,
            kNavButtonY);

  ShowImage(screens_path_.Append("qr_code.png"), kQrCodeX, kQrCodeY);
  int hwid_len = hwid_.size();
  int hwid_x = kQrCodeX + (kQrCodeSize / 2) + 16 + 5;
  const int hwid_y = kFooterY + kFooterLineHeight;

  if (right_to_left_) {
    hwid_x = -hwid_x - kMonospaceGlyphWidth * (hwid_len - 2);
  }

  ShowText(hwid_, hwid_x, hwid_y, "grey");
  ShowBox(kSeparatorX, kSeparatorY, 1, kQrCodeSize, kMenuGrey);
}

void Screens::MessageBaseScreen() {
  ClearMainArea();
  ShowLanguageMenu(false);
  ShowFooter();
}

void Screens::MiniOsWelcomeOnSelect() {
  MessageBaseScreen();
  ShowInstructionsWithTitle("MiniOS_welcome");
  ShowStepper({"1", "2", "3"});

  int index = 1;
  bool enter = false;
  ShowMiniOsWelcomeButtons(index);
  while (true) {
    // Get key events from evwaitkey.
    WaitMenuInput(3, &index, &enter);
    if (enter) {
      switch (index) {
        case 0:
          LanguageMenuOnSelect();
          // Return to current screen after picking new language.
          enter = false;
          index = 1;
          ShowInstructionsWithTitle("MiniOS_welcome");
          ShowStepper({"1", "2", "3"});
          break;
        case 1:
          MiniOsDropdownOnSelect();
          return;
        case 2:
          index = 1;
          ShowMiniOsWelcomeButtons(index);
          continue;
      }
    }
    // If not entered, update MiniOS Screen with new button selections.
    ShowMiniOsWelcomeButtons(index);
  }
}

void Screens::ShowMiniOsWelcomeButtons(int index) {
  ShowLanguageMenu(index == 0);
  constexpr int kBtnY = kTitleY + 80 + kBtnYStep * 2;
  ShowButton("btn_next", kBtnY, (index == 1), default_button_width_, false);
  ShowButton("btn_back", kBtnY + kBtnYStep, (index == 2), default_button_width_,
             false);
}

void Screens::MiniOsDropdownOnSelect() {
  MessageBaseScreen();
  ShowInstructions("title_MiniOS_dropdown");
  ShowStepper({"1-done", "2", "3"});

  int index = 1;
  ShowMiniOsDropdownButtons(index);
  bool enter = false;
  while (true) {
    WaitMenuInput(3, &index, &enter);
    if (enter) {
      switch (index) {
        case 0:
          LanguageMenuOnSelect();
          // Return to current screen after picking new language.
          ShowInstructions("title_MiniOS_dropdown");
          ShowStepper({"1-done", "2", "3"});
          enter = false;
          index = 1;
          break;
        case 1:
          ExpandItemDropdown();
          MiniOsGetPasswordOnSelect();
          return;
        case 2:
          MiniOsWelcomeOnSelect();
          return;
      }
    }
    ShowMiniOsDropdownButtons(index);
  }
}

void Screens::ShowMiniOsDropdownButtons(int index) {
  ShowLanguageMenu(index == 0);
  ShowCollapsedItemMenu((index == 1));
  ShowButton("btn_back", kTitleY + 58 + (4 * kBtnYStep), (index == 2),
             default_button_width_, false);
}

void Screens::ExpandItemDropdown() {
  SetItems();
  ShowLanguageMenu(false);
  ShowCollapsedItemMenu(true);

  int index = 0;
  ShowItemDropdown(index);
  bool enter = false;
  while (true) {
    WaitMenuInput(item_list_.size(), &index, &enter);
    if (enter && index > 0) {
      chosen_item_ = item_list_[index];
      LOG(INFO) << "Changed network to " << chosen_item_;
      return;
    }
    // Update drop down menu with new highlighted selection.
    ShowItemDropdown(index);
  }
}

void Screens::ShowMiniOsGetPasswordButtons(int index) {
  ShowLanguageMenu((index == 0));
  constexpr int kBtnY = kTitleY + 58 + kBtnYStep * 2;
  ShowButton("Enter your password", kBtnY, false, default_button_width_ * 4,
             true);
  ShowButton("btn_back", kBtnY + kBtnYStep, (index == 2), default_button_width_,
             false);
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

void Screens::MiniOsGetPasswordOnSelect() {
  MessageBaseScreen();
  ShowInstructionsWithTitle("MiniOS_password");
  ShowStepper({"done", "2-done", "3"});

  int index = 1;
  ShowMiniOsGetPasswordButtons(index);
  bool enter = false;
  while (true) {
    // Get key events from evwaitkey.
    WaitMenuInput(3, &index, &enter);
    if (enter) {
      switch (index) {
        case 0:
          LanguageMenuOnSelect();
          // Return to current screen after picking new language.
          ShowInstructionsWithTitle("MiniOS_password");
          ShowStepper({"done", "2-done", "3"});
          enter = false;
          index = 1;
          break;
        case 1:
          GetPassword();
          ShowMiniOsDownloading();
          return;
        case 2:
          MiniOsDropdownOnSelect();
          return;
      }
    }
    // If not entered, update MiniOS Screen with new button selections.
    ShowMiniOsGetPasswordButtons(index);
  }
}

void Screens::ShowMiniOsDownloading() {
  MessageBaseScreen();
  ShowInstructionsWithTitle("MiniOS_downloading");
  ShowStepper({"done", "done", "3-done"});
  ShowLanguageMenu(false);
  ShowProgressBar(10);
  ShowMiniOsComplete();
}

void Screens::ShowMiniOsComplete() {
  MessageBaseScreen();
  ShowInstructions("title_MiniOS_complete");
  ShowStepper({"done", "done", "done"});
  ShowLanguageMenu(false);
  ShowProgressBar(5);
  // TODO(vyshu): Automatically reboot after timeout or on button selection.
  ShowButton("Reboot", -100, false, default_button_width_, true);
}

void Screens::ReadDimensionConstants() {
  image_dimensions_.clear();
  base::FilePath path = screens_path_.Append(locale_).Append("constants.sh");
  std::string dimension_consts;
  if (!ReadFileToString(path, &dimension_consts)) {
    LOG(ERROR) << "Could not read constants.sh file for language " << locale_;
    return;
  }
  if (!base::SplitStringIntoKeyValuePairs(dimension_consts, '=', '\n',
                                          &image_dimensions_)) {
    LOG(WARNING) << "Unable to parse all dimension information for " << locale_;
    return;
  }

  // Save default button width for this locale.
  if (!GetDimension(kButtonWidthToken, &default_button_width_)) {
    default_button_width_ = kDefaultButtonWidth;
    LOG(WARNING) << "Unable to get dimension for " << kButtonWidthToken
                 << ". Defaulting to width " << kDefaultButtonWidth;
  }
}

bool Screens::GetDimension(const std::string& token, int* token_dimension) {
  if (image_dimensions_.empty()) {
    LOG(ERROR) << "No dimensions available.";
    return false;
  }

  // Find the dimension for the token.
  for (const auto& dimension : image_dimensions_) {
    if (dimension.first == token) {
      if (!base::StringToInt(dimension.second, token_dimension)) {
        LOG(ERROR) << "Could not convert " << dimension.second
                   << " to a number.";
        return false;
      }
      return true;
    }
  }
  return false;
}

void Screens::UpdateButtons(int menu_count, int key, int* index, bool* enter) {
  int starting_index = *index;
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
  *index = starting_index;
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

void Screens::ShowCollapsedItemMenu(bool is_selected) {
  constexpr int kOffsetY = -kCanvasSize / 2 + 350;
  constexpr int kBgX = -kCanvasSize / 2 + 145;
  constexpr int kGlobeX = -kCanvasSize / 2 + 20;
  constexpr int kArrowX = -kCanvasSize / 2 + 268;
  const int text_x = -kCanvasSize / 2 + 100;

  // Currently using language and globe icons as placeholders.
  base::FilePath menu_background =
      is_selected ? screens_path_.Append("language_menu_bg_focused.png")
                  : screens_path_.Append("language_menu_bg.png");

  ShowImage(menu_background, kBgX, kOffsetY);
  ShowImage(screens_path_.Append("ic_language-globe.png"), kGlobeX, kOffsetY);
  ShowImage(screens_path_.Append("ic_dropdown.png"), kArrowX, kOffsetY);
  ShowMessage("btn_MiniOS_display_options", text_x, kOffsetY);
}

void Screens::ShowItemDropdown(int index) {
  constexpr int kItemPerPage = 10;
  // Pick begin index such that the selected index is centered on the screen.
  int begin_index =
      std::clamp(index - kItemPerPage / 2, 0,
                 static_cast<int>(supported_locales_.size()) - kItemPerPage);

  int offset_y = -kCanvasSize / 2 + 350 + 40;
  constexpr int kBackgroundX = -kCanvasSize / 2 + 360;
  constexpr int kOffsetX = -kCanvasSize / 2 + 60;
  constexpr int kItemHeight = 40;
  for (int i = begin_index;
       i < (begin_index + kItemPerPage) && i < item_list_.size(); i++) {
    if (index == i) {
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
}

void Screens::CheckRightToLeft() {
  // TODO(vyshu): Create an unblocked_terms.txt to allow "he" for Hebrew.
  right_to_left_ = (locale_ == "ar" || locale_ == "fa" || locale_ == "he");
}

void Screens::CheckDetachable() {
  is_detachable_ =
      base::PathExists(root_.Append("etc/cros-initramfs/is_detachable"));
}

/*
vpd_get_value() {
  local file="/sys/firmware/vpd/ro/$1"

  if [ -e "${file}" ]; then
    cat "${file}"
  else
    vpd -g "$1"
  fi
}
*/
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

/*
read_truncated_hwid() {
  crossystem hwid | cut -f 1 -d ' '
}
*/
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
    LOG(ERROR) << "Could not read json. " << json_output.error_message
               << " and exit code "
               << base::JSONReader::ErrorCodeToString(json_output.error_code);
    return false;
  }

  // Look up mapping between vpd region and xkb keyboard layout.
  const base::Value* region_info = json_output.value->FindDictKey(vpd_region_);
  if (!region_info) {
    LOG(ERROR) << "Region " << vpd_region_ << " not found.";
    return false;
  }

  const base::Value* keyboard = region_info->FindListKey("keyboards");
  if (!keyboard || keyboard->GetList().empty()) {
    LOG(ERROR) << "Could not retrieve keyboards for given region "
               << vpd_region_
               << ". Available region information: " << *region_info;
    return false;
  }

  // Always use the first keyboard in the list.
  std::vector<std::string> keyboard_parts =
      base::SplitString(keyboard->GetList()[0].GetString(), ":",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (keyboard_parts.size() < 2) {
    LOG(ERROR) << "Could not parse keyboard information for region  "
               << vpd_region_;
    return false;
  }
  *xkb_keyboard_layout = keyboard_parts[1];
  return true;
}

void Screens::SetRootForTest(const std::string& test_root) {
  root_ = base::FilePath(test_root);
}

void Screens::SetLanguageForTest(const std::string& test_locale) {
  locale_ = test_locale;
  // Reload locale dependent dimension constants.
  ReadDimensionConstants();
}

void Screens::SetLocaleRtlForTest(bool is_rtl) {
  right_to_left_ = is_rtl;
}

}  // namespace screens
