// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_SCREEN_BASE_H_
#define MINIOS_SCREEN_BASE_H_

#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/strings/string_split.h>

namespace screens {

// Colors
extern const char kMenuBlack[];
extern const char kMenuBlue[];
extern const char kMenuGrey[];
extern const char kMenuButtonFrameGrey[];

// Dimension Constants
extern const int kMonospaceGlyphWidth;
extern const int kDefaultMessageWidth;

// Frecon constants
extern const int kFreconScalingFactor;
extern const int kCanvasSize;

// ScreenBase contains all the different components needed to show MiniOS
// Screens.
class ScreenBase {
 public:
  ScreenBase() = default;
  virtual ~ScreenBase() = default;
  // Not copyable or movable.
  ScreenBase(const ScreenBase&) = delete;
  ScreenBase& operator=(const ScreenBase&) = delete;

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
  virtual void ShowInstructionsWithTitle(const std::string& message_token);

  // Show progress bar at percentage given.
  void ShowProgressPercentage(double progress);

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

 protected:
  // Read dimension constants for current locale into memory. Must be updated
  // every time the language changes.
  void ReadDimensionConstants();

  // Sets the height or width of an image given the token. Returns false on
  // error.
  bool GetDimension(const std::string& token, int* token_dimension);

  // Gets frecon constants defined at initialization by Upstart job.
  void GetFreconConstants();

  // Whether the locale is read from right to left.
  bool right_to_left_{false};

  // Default button width. Changes for each locale.
  int default_button_width_;

  // Default root directory.
  base::FilePath root_{"/"};

  // Default screens path, set in init.
  base::FilePath screens_path_;

  // Default and fall back locale directory.
  std::string locale_{"en-US"};

  // Key value pairs that store token name and measurements.
  base::StringPairs image_dimensions_;

  int frecon_canvas_size_{1080};
  int frecon_scale_factor_{1};
};

}  // namespace screens

#endif  // MINIOS_SCREEN_BASE_H_
