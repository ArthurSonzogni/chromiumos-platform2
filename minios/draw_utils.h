// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_DRAW_UTILS_H_
#define MINIOS_DRAW_UTILS_H_

#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <gtest/gtest_prod.h>

#include "minios/draw_interface.h"
#include "minios/process_manager.h"
#include "minios/screen_types.h"

namespace minios {

// Dropdown Menu Colors.
extern const char kMenuBlack[];
extern const char kMenuBlue[];
extern const char kMenuGrey[];
extern const char kMenuDropdownFrameNavy[];
extern const char kMenuDropdownBackgroundBlack[];
extern const char kMenuButtonFrameGrey[];

// Dimension Constants
extern const int kButtonHeight;
extern const int kButtonMargin;
extern const int kDefaultMessageWidth;
extern const int kMonospaceGlyphHeight;
extern const int kMonospaceGlyphWidth;
extern const int kDefaultButtonWidth;
extern const int kSmallCanvasSize;
extern const int kProgressBarYScale;

// Frecon constants
extern const char kScreens[];
extern const int kFreconScalingFactor;
extern const int kCanvasSize;

// `DrawUtils` contains all the different components needed to show MiniOS
// Screens.
class DrawUtils : public DrawInterface {
 public:
  explicit DrawUtils(ProcessManagerInterface* process_manager)
      : process_manager_(process_manager),
        screens_path_(root_.Append(kScreens)) {
    // TODO(b/183791649): minios: Clean up. Replace screens_path_ with
    // GetScreenPath.
  }
  ~DrawUtils() override = default;
  // Not copyable or movable.
  DrawUtils(const DrawUtils&) = delete;
  DrawUtils& operator=(const DrawUtils&) = delete;

  bool Init() override;

  bool ShowText(const std::string& text,
                int glyph_offset_h,
                int glyph_offset_v,
                const std::string& color) override;

  bool ShowImage(const base::FilePath& image_name,
                 int offset_x,
                 int offset_y) override;

  bool ShowBox(int offset_x,
               int offset_y,
               int size_x,
               int size_y,
               const std::string& color) override;

  bool ShowMessage(const std::string& message_token,
                   int offset_x,
                   int offset_y) override;

  void ShowInstructions(const std::string& message_token) override;

  void ShowInstructionsWithTitle(const std::string& message_token) override;

  bool IsDetachable() override;

  bool IsLocaleRightToLeft() override;

  void ShowButton(const std::string& message_token,
                  int offset_y,
                  bool is_selected,
                  int inner_width,
                  bool is_text) override;

  void ShowStepper(const std::vector<std::string>& steps) override;

  void MessageBaseScreen() override;

  void ShowLanguageDropdown(int current_index) override;

  int FindLocaleIndex(int current_index) override;

  void ShowLanguageMenu(bool is_selected) override;

  void LocaleChange(int selected_locale) override;

  void ShowProgressPercentage(double progress) override;

  int GetSupportedLocalesSize() override { return supported_locales_.size(); }

  int GetDefaultButtonWidth() override { return default_button_width_; }

  int GetFreconCanvasSize() override { return frecon_canvas_size_; }

  base::FilePath GetScreenPath() override { return screens_path_; }

  // Override the root directory for testing. Default is '/'.
  void SetRootForTest(const std::string& test_root) {
    root_ = base::FilePath(test_root);
    screens_path_ = base::FilePath(root_).Append(kScreens);
  }

  // Override the current locale without using the language menu.
  void SetLanguageForTest(const std::string& test_locale) {
    locale_ = test_locale;
    // Reload locale dependent dimension constants.
    ReadDimensionConstants();
  }

 protected:
  FRIEND_TEST(DrawUtilsTest, InstructionsWithTitle);
  FRIEND_TEST(DrawUtilsTest, ReadDimension);
  FRIEND_TEST(DrawUtilsTest, GetDimension);
  FRIEND_TEST(DrawUtilsTest, GetLangConsts);
  FRIEND_TEST(DrawUtilsTest, GetLangConstsError);
  FRIEND_TEST(DrawUtilsTest, CheckRightToLeft);
  FRIEND_TEST(DrawUtilsTest, CheckDetachable);
  FRIEND_TEST(DrawUtilsTest, GetHwidFromCommand);
  FRIEND_TEST(DrawUtilsTest, GetHwidFromDefault);
  FRIEND_TEST(DrawUtilsTest, GetFreconConstFile);
  FRIEND_TEST(DrawUtilsTest, GetFreconConstNoInt);
  FRIEND_TEST(DrawUtilsTest, GetFreconConstNoFile);
  FRIEND_TEST(DrawUtilsTestMocks, ShowFooter);

  // Clears full screen except the footer.
  void ClearMainArea();

  // Clears screen including the footer.
  void ClearScreen();

  // Shows footer with basic instructions and chromebook model.
  void ShowFooter();

  // Read dimension constants for current locale into memory. Must be updated
  // every time the language changes.
  void ReadDimensionConstants();

  // Sets the height or width of an image given the token. Returns false on
  // error.
  bool GetDimension(const std::string& token, int* token_dimension);

  // Read the language constants into memory. Does not change
  // based on the current locale. Returns false on failure.
  bool ReadLangConstants();

  // Sets the width of language token for a given locale. Returns false on
  // error.
  bool GetLangConstants(const std::string& locale, int* lang_width);

  // Gets frecon constants defined at initialization by Upstart job.
  void GetFreconConstants();

  // Get hardware Id from crossystem. Set hwid to `CHROMEBOOK` as default.
  void ReadHardwareId();

  ProcessManagerInterface* process_manager_;

  int frecon_canvas_size_{1080};
  int frecon_scale_factor_{1};
  // Default button width. Changes for each locale.
  int default_button_width_{80};
  // Default root directory.
  base::FilePath root_{"/"};

  // Default screens path, set in init.
  base::FilePath screens_path_;

  // Default and fall back locale directory.
  std::string locale_{"en-US"};

  // Key value pairs that store token name and measurements.
  base::StringPairs image_dimensions_;

  // Key value pairs that store language widths.
  base::StringPairs lang_constants_;

  // List of all supported locales.
  std::vector<std::string> supported_locales_;

  // Hardware Id read from crossystem.
  std::string hwid_;

  // Whether the device has a detachable keyboard.
  bool is_detachable_{false};
};

}  // namespace minios

#endif  // MINIOS_DRAW_UTILS_H_
