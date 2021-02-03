// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREENS_H_
#define MINIOS_SCREENS_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/strings/string_split.h>
#include <gtest/gtest_prod.h>

#include "minios/key_reader.h"
#include "minios/process_manager.h"

namespace screens {

extern const char kScreens[];

// Colors.
extern const char kMenuBlack[];
extern const char kMenuBlue[];
extern const char kMenuGrey[];
extern const char kMenuButtonFrameGrey[];
extern const char kMenuDropdownFrameNavy[];
extern const char kMenuDropdownBackgroundBlack[];

// Key values.
extern const int kKeyUp;
extern const int kKeyDown;
extern const int kKeyEnter;
extern const int kKeyVolUp;
extern const int kKeyVolDown;
extern const int kKeyPower;

// Key state parameters.
extern const int kFdsMax;
extern const int kKeyMax;

class Screens : public key_reader::KeyReader::Delegate {
 public:
  explicit Screens(ProcessManagerInterface* process_manager)
      : key_states_(kFdsMax, std::vector<bool>(kKeyMax, false)) {
    process_manager_ = process_manager;
    key_reader_.SetDelegate(this);
  }
  virtual ~Screens() = default;
  // Not copyable or movable.
  Screens(const Screens&) = delete;
  Screens& operator=(const Screens&) = delete;

  // Loads token constants for screen placement, checks whether locale is read
  // from right to left and whether device is detachable.
  bool Init();

  // Has the minimum needed to set up tests, to reduce excessive logging. All
  // other components are tested separately.
  bool InitForTest();

  // Show dynamic text using pre-rendered glyphs. Colors 'white', 'grey' and
  // 'black'. Returns true on success.
  virtual bool ShowText(const std::string& text,
                        int glyph_offset_h,
                        int glyph_offset_v,
                        const std::string& color);

  // Uses frecon to show image given a full file path. Returns true on success.
  virtual bool ShowImage(const base::FilePath& image_name,
                         int offset_x,
                         int offset_y);

  // Uses frecon to show a box. Color should be given as a hex string. Returns
  // true on success.
  virtual bool ShowBox(int offset_x,
                       int offset_y,
                       int size_x,
                       int size_y,
                       const std::string& color);

  // Shows message image at the given offset. All message tokens are in
  // `/etc/screens`. Falls back to English if chosen locale is not available.
  virtual bool ShowMessage(const std::string& message_token,
                           int offset_x,
                           int offset_y);

  // Shows title and uses title offsets.
  void ShowInstructions(const std::string& message_token);

  // Shows the title and corresponding description using offsets from
  // `constants` to place.
  void ShowInstructionsWithTitle(const std::string& message_token);

  // Shows on screen progress bar.
  void ShowProgressBar(double seconds);

  // Clears full screen except the footer.
  void ClearMainArea();

  // Clears screen including the footer.
  void ClearScreen();

  // Show button, focus changes the button color to indicate selection. Returns
  // false on error.
  void ShowButton(const std::string& message_token,
                  int offset_y,
                  bool is_selected,
                  int inner_width,
                  bool is_text);

  // Shows stepper icons given a list of steps. Currently icons available in
  // 'kScreens' only go up to 3. Steps can be a number '1', 'error', or 'done'.
  // Defaults to done if requested icon not found.
  void ShowStepper(const std::vector<std::string>& steps);

  // Shows the MiniOs Screens. Users can navigate between then using up/down
  // arrow keys.
  void StartMiniOsFlow();

  // Shows the list of all supported locales with the currently selected index
  // highlighted blue. Users can 'scroll' using the up and down arrow keys.
  void ShowLanguageDropdown();

  // Waits for key input and repaints the screen with a changed language
  // selection, clears the whole screen including the footer and updates the
  // language dependent constants. Returns to original screen after selection,
  virtual void LanguageMenuOnSelect();

  // Shows language menu drop down button on base screen. Button is highlighted
  // if it is currently selected.
  void ShowLanguageMenu(bool is_selected);

  // Shows footer with basic instructions and chromebook model.
  void ShowFooter();

  // Clears screen and shows footer and language drop down menu.
  void MessageBaseScreen();

  // Shows a list of all available items.
  void ShowItemDropdown();

  // Shows item menu drop down button on the dropdown screen. Button is
  // highlighted if it is currently selected. Selecting this button directs to
  // the expanded dropdown.
  void ShowCollapsedItemMenu(bool is_selected);

  // Queries list of available items and shows them as a drop down. On
  // selection sets the 'chosen_item' and redirects to the password
  // screen.
  void ExpandItemDropdown();

  // Get user password using the keyboard layout stored in locale. Users can use
  // the tab key to toggle showing the password.
  virtual void GetPassword();

  // Controls the flow of MiniOs by changing screen based on the current index
  // and screen and whether or not a button has been selected(entered). Called
  // every time a valid key press is recorded.
  void SwitchScreen(bool enter);

  // Override the root directory for testing. Default is '/'.
  void SetRootForTest(const std::string& test_root) {
    root_ = base::FilePath(test_root);
  }

  // Override the current locale without using the language menu.
  void SetLanguageForTest(const std::string& test_locale) {
    locale_ = test_locale;
    // Reload locale dependent dimension constants.
    ReadDimensionConstants();
  }

  // Override whether current language is marked as being read from right to
  // left. Does not change language.
  void SetLocaleRtlForTest(bool is_rtl) { right_to_left_ = is_rtl; }

  // Getter and setter test functions for `index_` and `current_screen`.
  void SetIndexForTest(int index) { index_ = index; }
  int GetIndexForTest() { return index_; }
  void SetScreenForTest(int current_screen) {
    current_screen_ = static_cast<ScreenType>(current_screen);
  }
  int GetScreenForTest() { return static_cast<int>(current_screen_); }

 private:
  FRIEND_TEST(ScreensTest, ReadDimension);
  FRIEND_TEST(ScreensTest, GetDimension);
  FRIEND_TEST(ScreensTest, GetLangConsts);
  FRIEND_TEST(ScreensTest, UpdateButtons);
  FRIEND_TEST(ScreensTest, UpdateButtonsIsDetachable);
  FRIEND_TEST(ScreensTest, CheckRightToLeft);
  FRIEND_TEST(ScreensTest, CheckDetachable);
  FRIEND_TEST(ScreensTest, GetVpdFromFile);
  FRIEND_TEST(ScreensTest, GetVpdFromCommand);
  FRIEND_TEST(ScreensTest, GetVpdFromDefault);
  FRIEND_TEST(ScreensTest, GetHwidFromCommand);
  FRIEND_TEST(ScreensTest, GetHwidFromDefault);
  FRIEND_TEST(ScreensTest, MapRegionToKeyboardNoFile);
  FRIEND_TEST(ScreensTest, MapRegionToKeyboardNotDict);
  FRIEND_TEST(ScreensTest, MapRegionToKeyboardNoKeyboard);
  FRIEND_TEST(ScreensTest, MapRegionToKeyboardBadKeyboardFormat);
  FRIEND_TEST(ScreensTest, MapRegionToKeyboard);
  FRIEND_TEST(ScreensTestMocks, OnKeyPress);

  ProcessManagerInterface* process_manager_;

  key_reader::KeyReader key_reader_ =
      key_reader::KeyReader(/*include_usb=*/true);

  // Read dimension constants for current locale into memory. Must be updated
  // every time the language changes.
  void ReadDimensionConstants();

  // Sets the height or width of an image given the token. Returns false on
  // error.
  bool GetDimension(const std::string& token, int* token_dimension);

  // Changes the index and enter value based on the given key. Unknown keys are
  // ignored and index is kept within the range of menu items.
  void UpdateButtons(int menu_count, int key, bool* enter);

  // Read the language constants into memory. Does not change
  // based on the current locale.
  void ReadLangConstants();

  // Sets the width of language token for a given locale. Returns false on
  // error.
  bool GetLangConstants(const std::string& locale, int* lang_width);

  // This function overloads Delegate. It is only called when the key is valid
  // and updates the key state for the given fd and key. Calls `SwitchState` to
  // update the flow once key is recorded as being pressed and released.
  void OnKeyPress(int fd_index, int key_changed, bool key_released) override;

  // Does all the reloading needed when the locale is changed, including
  // repainting the screen. Called after `LanguageDropdown` is done.
  virtual void OnLocaleChange();

  // Calls the show screen function of `current_screen`.
  virtual void ShowNewScreen();

  // Shows the buttons of MiniOs screens. Index changes button focus based on
  // button order.
  void ShowMiniOsWelcomeScreen();
  void ShowMiniOsDropdownScreen();
  void ShowMiniOsGetPasswordScreen();
  void ShowMiniOsDownloadingScreen();
  void ShowMiniOsCompleteScreen();

  // Sets list of available items to `item_list_` to show in drop down. Called
  // every time the menu is clicked.
  void SetItems();

  // Checks whether the current language is read from right to left. Must be
  // updated every time the language changes.
  void CheckRightToLeft();

  // Checks whether device has a detachable keyboard and sets `is_detachable`.
  void CheckDetachable();

  // Get region from VPD. Set vpd_region_ to US as default.
  void GetVpdRegion();

  // Get hardware Id from crossystem. Set hwid to `CHROMEBOOK` as default.
  void ReadHardwareId();

  // Get XKB keyboard layout based on the VPD region. Return false on error.
  bool MapRegionToKeyboard(std::string* xkb_keyboard_layout);

  // Whether the locale is read from right to left.
  bool right_to_left_{false};

  // Whether the device has a detachable keyboard.
  bool is_detachable_{false};

  // Key value pairs that store token name and measurements.
  base::StringPairs image_dimensions_;

  // Key value pairs that store language widths.
  base::StringPairs lang_constants_;

  // List of all supported locales.
  std::vector<std::string> supported_locales_;

  // List of currently available items.
  std::vector<std::string> item_list_;

  // The item the user has picked from the dropdown menu.
  std::string chosen_item_;

  // Default button width. Changes for each locale.
  int default_button_width_;

  // Default root directory.
  base::FilePath root_{"/"};

  // Default screens path, set in init.
  base::FilePath screens_path_;

  // Default and fall back locale directory.
  std::string locale_{"en-US"};

  // Hardware Id read from crossystem.
  std::string hwid_;

  // Region code read from VPD. Used to determine keyboard layout. Does not
  // change based on selected locale.
  std::string vpd_region_;

  // Records the key press for each fd and key, where the index of the fd is the
  // row and the key code the column. Resets to false after key is released.
  // Only tracks the valid keys.
  std::vector<std::vector<bool>> key_states_;

  // The number of menu buttons on each screen corresponding to the enum
  // numbers, used to keep the index in bounds. The dropdown menu counts are
  // updated based on the number of items in the dropdown.
  std::vector<int> menu_count_{3, 3, 0, 3, 0, 0};

  // All the different screens in the MiniOs Flow.
  enum class ScreenType {
    kWelcomeScreen = 0,
    kDropDownScreen = 1,
    kExpandedDropDownScreen = 2,
    kPasswordScreen = 3,
    kLanguageDropDownScreen = 4,
    kDoneWithFlow = 5
  };

  ScreenType current_screen_{ScreenType::kWelcomeScreen};
  // Previous screen only used when changing the language so you know what
  // screen to return to after selection.
  ScreenType previous_screen_{ScreenType::kWelcomeScreen};

  // The `index_` shows which button is highlighted in the `current_screen_`,
  // uses menu_count of current screen to stay in bounds.
  int index_{1};
};

}  // namespace screens

#endif  // MINIOS_SCREENS_H_
