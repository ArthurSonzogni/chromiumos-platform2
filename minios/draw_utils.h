// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_DRAW_UTILS_H_
#define MINIOS_DRAW_UTILS_H_

#include <absl/container/flat_hash_map.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_split.h>
#include <base/time/time.h>
#include <base/timer/timer.h>
#include <gtest/gtest_prod.h>

#include "minios/draw_utils.h"
#include "minios/process_manager_interface.h"

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
extern const int kProgressBarHeight;

// Frecon constants
extern const char kScreens[];
extern const int kFreconScalingFactor;
extern const int kCanvasSize;
extern const int kFreconNoOffset;

extern const char kDetachablePath[];

class DrawInterface {
 public:
  virtual ~DrawInterface() = default;

  virtual bool Init() = 0;

  // Show dynamic text using pre-rendered glyphs. Colors 'white', 'grey' and
  // 'black'. Returns true on success.
  virtual bool ShowText(const std::string& text,
                        int glyph_offset_h,
                        int glyph_offset_v,
                        const std::string& color) = 0;
  // Shows image at offsets given a full file path. Returns true on success.
  virtual bool ShowImage(const base::FilePath& image_name,
                         int offset_x,
                         int offset_y) = 0;
  // Shows a box at given offsets. Color should be given as a hex string.
  // Returns true on success.
  virtual bool ShowBox(int offset_x,
                       int offset_y,
                       int size_x,
                       int size_y,
                       const std::string& color) = 0;

  // Shows message image at the given offset. All message tokens are in
  // `/etc/screens`. Falls back to English if chosen locale is not available.
  virtual bool ShowMessage(const std::string& message_token,
                           int offset_x,
                           int offset_y) = 0;

  // Shows title and uses title offsets.
  virtual void ShowInstructions(const std::string& message_token) = 0;

  // Shows the title and corresponding description using offsets from
  // `constants` to place.
  virtual void ShowInstructionsWithTitle(const std::string& message_token) = 0;

  // Returns whether device has a detachable keyboard.
  virtual bool IsDetachable() const = 0;

  // Show button, focus changes the button color to indicate selection. Returns
  // false on error.
  virtual void ShowButton(const std::string& message_token,
                          int offset_y,
                          bool is_selected,
                          int inner_width,
                          bool is_text) = 0;

  // Shows stepper icons given a list of steps. Currently icons available in
  // 'kScreens' only go up to 3. Steps can be a number '1', 'error', or 'done'.
  // Defaults to done if requested icon not found.
  virtual void ShowStepper(const std::vector<std::string>& steps) = 0;

  // Shows advanced options button at the bottom of the screen.
  virtual void ShowAdvancedOptionsButton(bool focused) = 0;

  // Shows power button at the bottom of the screen.
  virtual void ShowPowerButton(bool focused) = 0;

  // Clears screen and shows footer and language drop down menu.
  virtual void MessageBaseScreen() = 0;

  // Shows the language dropdown button.
  virtual void ShowLanguageDropdown(int current_index) = 0;

  // Find the index of currently selected locale.
  virtual int FindLocaleIndex() const = 0;

  // Shows language menu drop down button on base screen. Button is highlighted
  // if it is currently selected.
  virtual void ShowLanguageMenu(bool is_selected) = 0;

  // Does all the reloading needed when the locale is changed, including
  // repainting the screen. Called after `LanguageDropdown` is done.
  virtual void LocaleChange(int selected_locale) = 0;

  // Show an empty progress bar.
  virtual void ShowProgressBar() = 0;

  // Show progress bar at percentage given.
  virtual void ShowProgressPercentage(double progress) = 0;

  // Show indeterminate progress bar.
  virtual void ShowIndeterminateProgressBar() = 0;

  // Hide/Stop indeterminate progress bar.
  virtual void HideIndeterminateProgressBar() = 0;

  // Returns number of locales.
  virtual int GetSupportedLocalesSize() const = 0;

  // Returns the default button width, read from constants.
  virtual int GetDefaultButtonWidth() const = 0;

  // Returns the frecon canvas size.
  virtual int GetFreconCanvasSize() const = 0;

  // Returns the screen assets path.
  virtual base::FilePath GetScreensPath() const = 0;

  // Returns whether the current locale is read from right to left.
  virtual bool IsLocaleRightToLeft() const = 0;

  // Show a closed drop down menu at the specified location.
  virtual void ShowDropDownClosed(int offset_x,
                                  int offset_y,
                                  int text_x,
                                  const std::string& message,
                                  const std::string& icon_label,
                                  bool is_selected) = 0;
};

// `DrawUtils` contains all the different components needed to show MiniOS
// Screens.
class DrawUtils : public DrawInterface {
 public:
  // The period corresponding to 66.67 fps.
  static constexpr base::TimeDelta kAnimationPeriod = base::Milliseconds(15);

  explicit DrawUtils(std::shared_ptr<ProcessManagerInterface> process_manager)
      : process_manager_(process_manager),
        screens_path_(root_.Append(kScreens)) {}
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

  bool IsDetachable() const override;

  bool IsLocaleRightToLeft() const override;

  void ShowButton(const std::string& message_token,
                  int offset_y,
                  bool is_selected,
                  int inner_width,
                  bool is_text) override;

  void ShowStepper(const std::vector<std::string>& steps) override;

  void MessageBaseScreen() override;

  void ShowLanguageDropdown(int current_index) override;

  int FindLocaleIndex() const override;

  void ShowLanguageMenu(bool is_selected) override;

  void ShowAdvancedOptionsButton(bool focused) override;

  void ShowPowerButton(bool focused) override;

  void LocaleChange(int selected_locale) override;

  void ShowProgressBar() override;

  void ShowProgressPercentage(double progress) override;

  void ShowIndeterminateProgressBar() override;

  void HideIndeterminateProgressBar() override;

  int GetSupportedLocalesSize() const override {
    return supported_locales_.size();
  }

  int GetDefaultButtonWidth() const override { return default_button_width_; }

  int GetFreconCanvasSize() const override { return frecon_canvas_size_; }

  base::FilePath GetScreensPath() const override { return screens_path_; }

  // Override the root directory for testing. Default is '/'.
  void SetRootForTest(const std::string& test_root) {
    root_ = base::FilePath(test_root);
    screens_path_ = base::FilePath(root_).Append(kScreens);
    is_detachable_ = base::PathExists(root_.Append(kDetachablePath));
  }

  // Override the current locale without using the language menu.
  void SetLanguageForTest(const std::string& test_locale) {
    locale_ = test_locale;
    // Reload locale dependent dimension constants.
    ReadDimensionConstants();
  }
  // Draw closed drop down at `offset_x  offset_y`.  Place `message` at `text_x`
  // alongside `icon_label` which should be an image file name.
  void ShowDropDownClosed(int offset_x,
                          int offset_y,
                          int text_x,
                          const std::string& message,
                          const std::string& icon_label,
                          bool is_selected) override;

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
  FRIEND_TEST(DrawUtilsTestMocks, ShowInvalidVersion);
  FRIEND_TEST(DrawUtilsTestMocks, ShowLeftToRightVersion);
  FRIEND_TEST(DrawUtilsTestMocks, ShowRightToLeftVersion);

  // Shows a progress bar (box of a predetermined location) at the given offset
  // with the given size. Color should be given as a hex string. Acts as a No-op
  // if offset is outside the bounds of the canvas. Will also clamp progress bar
  // to the bounds of the canvas.
  void ShowProgressBar(int offset_x, int size_x, const std::string& color);
  // Initialize the segments and offsets for the head and tail of the
  // indeterminate progress bar.
  void InitIndeterminateProgressBar();
  // Reset the offsets for the head and tail of the indeterminate progress bar
  // to starting positions.
  void ResetIndeterminateProgressBar();
  // Draw the next segment of the indeterminate progress bar.
  void DrawIndeterminateProgressBar();

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
  bool GetDimension(const std::string& token, int* token_dimension) const;

  // Read the language constants into memory. Does not change
  // based on the current locale. Returns false on failure.
  bool ReadLangConstants();

  // Sets the width of language token for a given locale. Returns false on
  // error.
  bool GetLangConstants(const std::string& locale, int* lang_width) const;

  // Gets frecon constants defined at initialization by Upstart job.
  void GetFreconConstants();

  // Get hardware Id from crossystem. Set hwid to `CHROMEBOOK` as default.
  void ReadHardwareId();

  // Show minios version in UI.
  void ShowVersion();

  // Show non navigational buttons. These buttons don't have a box, and can have
  // icons or arrows next to them.
  void ShowControlButton(const std::optional<std::string>& icon,
                         const std::string& token,
                         int x_offset,
                         int y_offset,
                         int button_width,
                         bool show_arrow,
                         bool focused);

  std::shared_ptr<ProcessManagerInterface> process_manager_;

  // Timer for animating the indeterminate progress bar.
  base::RepeatingTimer timer_;

  int frecon_canvas_size_{1080};
  int frecon_scale_factor_{1};
  // This is always half of `frecon_canvas_size` since offsets are always
  // relative to the center of the screen and thus go from
  // `-frecon_offset_limit` to `+frecon_offset_limit`.
  int frecon_offset_limit_{540};
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
  absl::flat_hash_map<std::string, int> language_widths_;

  // List of all supported locales.
  std::vector<std::string> supported_locales_;

  // Hardware Id read from crossystem.
  std::string hwid_;

  // X-offsets for the current head and tail of the indeterminate progress bar.
  int indeterminate_progress_bar_head_;
  int indeterminate_progress_bar_tail_;
  // Per frame segment size for the head and tail of the indeterminate progress
  // bar.
  int segment_size_head_;
  int segment_size_tail_;

  // Whether the device has a detachable keyboard.
  bool is_detachable_{false};

  // The version parsed from cmdline, nullopt on failure.
  std::optional<std::string> minios_version_;
};

}  // namespace minios

#endif  // MINIOS_DRAW_UTILS_H_
