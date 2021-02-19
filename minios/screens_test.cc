// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/file_utils.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "minios/mock_process_manager.h"
#include "minios/screens.h"

using testing::_;

namespace screens {

namespace {

constexpr char kCrosJsonSnippet[] =
    "{\"au\": {\"region_code\": \"au\", \"confirmed\": true, "
    "\"description\": \"Australia\", \"keyboards\": [\"xkb:us::eng\"], "
    "\"time_zones\": [\"Australia/Sydney\"], \"locales\": [\"en-AU\"], "
    "\"keyboard_mechanical_layout\": \"ANSI\", \"regulatory_domain\": "
    "\"AU\"}, \"be\": {\"region_code\": \"be\", \"confirmed\": true, "
    "\"description\": \"Belgium\", \"keyboards\": [\"xkb:be::nld\", "
    "\"xkb:ca:eng:eng\"], \"time_zones\": [\"Europe/Brussels\"], "
    "\"locales\": [\"en-GB\"], \"keyboard_mechanical_layout\": \"ISO\", "
    "\"regulatory_domain\": \"BE\"},  \"he\": {\"keyboards\": [\"xkbbenld\"]}, "
    "\"us\": {\"region_code\": \"us\", \"confirmed\": true, "
    "\"description\": \"US\"}}";

}  // namespace

class ScreensTest : public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    test_root_ = temp_dir_.GetPath().value();
    screens_.SetRootForTest(test_root_);

    screens_path_ = base::FilePath(test_root_).Append(kScreens);

    base::FilePath locale_dir_en =
        base::FilePath(screens_path_).Append("en-US");
    ASSERT_TRUE(base::CreateDirectory(locale_dir_en));
    base::FilePath locale_dir_fr = base::FilePath(screens_path_).Append("fr");
    ASSERT_TRUE(CreateDirectory(locale_dir_fr));
    // Create and write constants file.
    std::string token_consts =
        "TITLE_minios_token_HEIGHT=38 \nDESC_minios_token_HEIGHT=44\n"
        "DESC_screen_token_HEIGHT=incorrect\nDEBUG_OPTIONS_BTN_WIDTH=99\n";
    ASSERT_TRUE(
        base::WriteFile(locale_dir_en.Append("constants.sh"), token_consts));

    // Create directories.
    ASSERT_TRUE(
        base::CreateDirectory(base::FilePath(test_root_).Append("dev/pts")));
    console_ = base::FilePath(test_root_).Append("dev/pts/0");
    ASSERT_TRUE(base::WriteFile(console_, ""));
    ASSERT_TRUE(CreateDirectory(
        base::FilePath(screens_path_).Append("glyphs").Append("white")));
    ASSERT_TRUE(CreateDirectory(
        base::FilePath(test_root_).Append("sys/firmware/vpd/ro")));
    ASSERT_TRUE(base::CreateDirectory(
        base::FilePath(test_root_).Append("usr/share/misc")));
    ASSERT_TRUE(screens_.InitForTest());
  }

 protected:
  // Test directory.
  base::ScopedTempDir temp_dir_;
  // Path to output pts.
  base::FilePath console_;
  // Path to /etc/screens in test directory.
  base::FilePath screens_path_;
  MockProcessManager mock_process_manager_;
  Screens screens_{&mock_process_manager_};
  std::string test_root_;
};

TEST_F(ScreensTest, ShowText) {
  EXPECT_TRUE(screens_.ShowText("chrome", 200, -100, "white"));

  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  std::string expected_command =
      "\x1B]image:file=" + test_root_ + "/etc/screens/glyphs/" +
      "white/99.png;offset=200,-100;scale=1\a\x1B]image:file=" + test_root_ +
      "/etc/screens/glyphs/white/"
      "104.png;offset=210,-100;scale=1\a\x1B]image:file=" +
      test_root_ +
      "/etc/screens/glyphs/white/"
      "114.png;offset=220,-100;scale=1\a\x1B]image:file=" +
      test_root_ +
      "/etc/screens/glyphs/white/"
      "111.png;offset=230,-100;scale=1\a\x1B]image:file=" +
      test_root_ +
      "/etc/screens/glyphs/white/"
      "109.png;offset=240,-100;scale=1\a\x1B]image:file=" +
      test_root_ +
      "/etc/screens/glyphs/white/"
      "101.png;offset=250,-100;scale=1\a";
  EXPECT_EQ(expected_command, written_command);
}

TEST_F(ScreensTest, ShowImageTest) {
  EXPECT_TRUE(screens_.ShowImage(base::FilePath(test_root_).Append("image.png"),
                                 50, 20));

  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  EXPECT_EQ(
      "\x1B]image:file=" + test_root_ + "/image.png;offset=50,20;scale=1\a",
      written_command);
}

TEST_F(ScreensTest, ShowImageRtl) {
  screens_.SetLocaleRtlForTest(true);
  EXPECT_TRUE(screens_.ShowImage(base::FilePath(test_root_).Append("image.png"),
                                 50, 10));

  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  EXPECT_EQ(
      "\x1B]image:file=" + test_root_ + "/image.png;offset=-50,10;scale=1\a",
      written_command);
}

TEST_F(ScreensTest, ShowBox) {
  EXPECT_TRUE(screens_.ShowBox(-100, -200, 50, 40, "0x8AB4F8"));
  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  EXPECT_EQ("\x1B]box:color=0x8AB4F8;size=50,40;offset=-100,-200;scale=1\a",
            written_command);
}

TEST_F(ScreensTest, ShowBoxRtl) {
  // Set locale to be read right to left.
  screens_.SetLocaleRtlForTest(true);
  EXPECT_TRUE(screens_.ShowBox(-100, -200, 50, 20, "0x8AB4F8"));
  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  // X offset should be inverted.
  EXPECT_EQ("\x1B]box:color=0x8AB4F8;size=50,20;offset=100,-200;scale=1\a",
            written_command);
}

TEST_F(ScreensTest, ShowMessage) {
  brillo::TouchFile(screens_path_.Append("fr").Append("minios_token.png"));

  // Override language to french.
  screens_.SetLanguageForTest("fr");
  EXPECT_TRUE(screens_.ShowMessage("minios_token", 0, 20));

  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  EXPECT_EQ("\x1B]image:file=" + test_root_ +
                "/etc/screens/fr/minios_token.png;offset=0,20;scale=1\a",
            written_command);
}

TEST_F(ScreensTest, ShowMessageFallback) {
  // Create french and english image files.
  brillo::TouchFile(screens_path_.Append("fr").Append("not_minios_token.png"));
  brillo::TouchFile(screens_path_.Append("en-US").Append("minios_token.png"));

  // Override language to french.
  screens_.SetLanguageForTest("fr");
  EXPECT_TRUE(screens_.ShowMessage("minios_token", 0, 20));

  // French token does not exist, fall back to english token.
  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  EXPECT_EQ("\x1B]image:file=" + test_root_ +
                "/etc/screens/en-US/minios_token.png;offset=0,20;scale=1\a",
            written_command);
}

TEST_F(ScreensTest, InstructionsWithTitle) {
  // Create english title and description tokens.
  brillo::TouchFile(
      screens_path_.Append("en-US").Append("title_minios_token.png"));
  brillo::TouchFile(
      screens_path_.Append("en-US").Append("desc_minios_token.png"));

  screens_.ShowInstructionsWithTitle("minios_token");

  std::string written_command;
  EXPECT_TRUE(ReadFileToString(console_, &written_command));
  std::string expected_command =
      "\x1B]image:file=" + test_root_ +
      "/etc/screens/en-US/"
      "title_minios_token.png;offset=-180,-301;scale=1\a\x1B]image:file=" +
      test_root_ +
      "/etc/screens/en-US/desc_minios_token.png;offset=-180,-244;scale=1\a";

  EXPECT_EQ(expected_command, written_command);
}

TEST_F(ScreensTest, ReadDimension) {
  std::string token_consts =
      "TITLE_minios_token_HEIGHT=\nDESC_minios_token_HEIGHT=44\nDESC_"
      "screen_token_HEIGHT=incorrect\n screen_whitespace_HEIGHT=  77  \n";
  ASSERT_TRUE(base::WriteFile(
      base::FilePath(screens_path_).Append("fr").Append("constants.sh"),
      token_consts));

  // Loads French dimension constants into memory.
  screens_.SetLanguageForTest("fr");

  EXPECT_EQ(4, screens_.image_dimensions_.size());
  EXPECT_EQ("  77", screens_.image_dimensions_[3].second);
}

TEST_F(ScreensTest, GetDimension) {
  int dimension;
  EXPECT_FALSE(screens_.GetDimension("DESC_invalid_HEIGHT", &dimension));
  EXPECT_FALSE(
      screens_.GetDimension("incorrect_DESC_minios_token_HEIGHT", &dimension));
  // Not a number.
  EXPECT_FALSE(screens_.GetDimension("DESC_screen_token_HEIGHT", &dimension));

  // Correctly returns the dimension.
  EXPECT_TRUE(screens_.GetDimension("TITLE_minios_token_HEIGHT", &dimension));
  EXPECT_EQ(38, dimension);
}

TEST_F(ScreensTest, GetLangConsts) {
  std::string lang_consts =
      "LANGUAGE_en_US_WIDTH=99\nLANGUAGE_fi_WIDTH=44\nLANGUAGE_mr_WIDTH="
      "incorrect\n LANGUAGE_ko_WIDTH=  77 \n  SUPPORTED_LOCALES=\"en-US fi mr "
      "ko\"";
  ASSERT_TRUE(
      base::WriteFile(screens_path_.Append("lang_constants.sh"), lang_consts));
  screens_.ReadLangConstants();

  EXPECT_EQ(5, screens_.lang_constants_.size());
  EXPECT_EQ(4, screens_.supported_locales_.size());
  int lang_width;
  EXPECT_TRUE(screens_.GetLangConstants("en-US", &lang_width));
  EXPECT_EQ(99, lang_width);
  // Incorrect or doesn't exist.
  EXPECT_FALSE(screens_.GetLangConstants("fr", &lang_width));
  EXPECT_FALSE(screens_.GetLangConstants("mr", &lang_width));
}

TEST_F(ScreensTest, UpdateButtons) {
  screens_.SetIndexForTest(1);
  int menu_items = 4;
  bool enter = false;
  screens_.UpdateButtons(menu_items, kKeyUp, &enter);
  EXPECT_EQ(0, screens_.GetIndexForTest());

  // Test range.
  screens_.UpdateButtons(menu_items, kKeyUp, &enter);
  EXPECT_EQ(0, screens_.GetIndexForTest());
  // Move to last item.
  screens_.SetIndexForTest(menu_items - 1);
  screens_.UpdateButtons(menu_items, kKeyDown, &enter);
  EXPECT_EQ(menu_items - 1, screens_.GetIndexForTest());
  EXPECT_FALSE(enter);
  // Enter key pressed.
  screens_.SetIndexForTest(1);
  screens_.UpdateButtons(menu_items, kKeyEnter, &enter);
  EXPECT_EQ(1, screens_.GetIndexForTest());
  EXPECT_TRUE(enter);

  // Unknown key, no action taken.
  screens_.SetIndexForTest(2);
  enter = false;
  screens_.UpdateButtons(menu_items, 89, &enter);
  EXPECT_EQ(2, screens_.GetIndexForTest());
  EXPECT_FALSE(enter);

  // If index somehow goes out of range, reset to 0.
  screens_.SetIndexForTest(menu_items + 5);
  enter = false;
  screens_.UpdateButtons(menu_items, kKeyEnter, &enter);
  EXPECT_EQ(0, screens_.GetIndexForTest());
}

TEST_F(ScreensTest, UpdateButtonsIsDetachable) {
  screens_.SetIndexForTest(1);
  bool enter = false;
  int menu_items = 4;

  screens_.UpdateButtons(menu_items, kKeyVolUp, &enter);
  EXPECT_EQ(0, screens_.GetIndexForTest());

  // Test range.
  screens_.UpdateButtons(menu_items, kKeyVolUp, &enter);
  EXPECT_EQ(0, screens_.GetIndexForTest());
  // Move to last item.
  screens_.SetIndexForTest(menu_items - 1);
  screens_.UpdateButtons(menu_items, kKeyVolDown, &enter);
  EXPECT_EQ(3, screens_.GetIndexForTest());
  EXPECT_FALSE(enter);
  // Enter key pressed.
  screens_.SetIndexForTest(1);
  screens_.UpdateButtons(menu_items, kKeyPower, &enter);
  EXPECT_EQ(1, screens_.GetIndexForTest());
  EXPECT_TRUE(enter);
}

TEST_F(ScreensTest, CheckRightToLeft) {
  screens_.SetLanguageForTest("fr");
  screens_.CheckRightToLeft();
  EXPECT_FALSE(screens_.right_to_left_);

  // Three languages are read from right to left.
  screens_.SetLanguageForTest("he");
  screens_.CheckRightToLeft();
  EXPECT_TRUE(screens_.right_to_left_);

  screens_.SetLanguageForTest("fa");
  screens_.CheckRightToLeft();
  EXPECT_TRUE(screens_.right_to_left_);

  screens_.SetLanguageForTest("ar");
  screens_.CheckRightToLeft();
  EXPECT_TRUE(screens_.right_to_left_);
}

TEST_F(ScreensTest, CheckDetachable) {
  screens_.CheckDetachable();

  EXPECT_FALSE(screens_.is_detachable_);

  brillo::TouchFile(
      base::FilePath(test_root_).Append("etc/cros-initramfs/is_detachable"));
  screens_.CheckDetachable();
  EXPECT_TRUE(screens_.is_detachable_);
}

TEST_F(ScreensTest, GetVpdFromFile) {
  std::string vpd = "ca";
  ASSERT_TRUE(base::WriteFile(
      base::FilePath(test_root_).Append("sys/firmware/vpd/ro/region"), vpd));
  screens_.GetVpdRegion();
  EXPECT_EQ(screens_.vpd_region_, "ca");
}

TEST_F(ScreensTest, GetVpdFromCommand) {
  std::string output = "ca";
  EXPECT_CALL(mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(testing::DoAll(testing::SetArgPointee<2>(output),
                               testing::Return(true)));
  screens_.GetVpdRegion();
  EXPECT_EQ(screens_.vpd_region_, output);
}

TEST_F(ScreensTest, GetVpdFromDefault) {
  EXPECT_CALL(mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(testing::Return(false));
  screens_.GetVpdRegion();
  EXPECT_EQ(screens_.vpd_region_, "us");
}

TEST_F(ScreensTest, GetHwidFromCommand) {
  std::string output = "Nightfury TEST ID";
  EXPECT_CALL(mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(testing::DoAll(testing::SetArgPointee<2>(output),
                               testing::Return(true)));
  screens_.ReadHardwareId();
  // Returns truncated hwid.
  EXPECT_EQ(screens_.hwid_, "Nightfury");
}

TEST_F(ScreensTest, GetHwidFromDefault) {
  EXPECT_CALL(mock_process_manager_, RunCommandWithOutput(_, _, _, _))
      .WillOnce(testing::Return(false));
  screens_.ReadHardwareId();
  EXPECT_EQ(screens_.hwid_, "CHROMEBOOK");
}

TEST_F(ScreensTest, MapRegionToKeyboardNoFile) {
  std::string keyboard;
  EXPECT_FALSE(screens_.MapRegionToKeyboard(&keyboard));
  EXPECT_TRUE(keyboard.empty());
}

TEST_F(ScreensTest, GetFreconConstFile) {
  std::string frecon_scale_factor = "2";
  std::string frecon_canvas_size = "1100";
  ASSERT_TRUE(CreateDirectory(
      base::FilePath(test_root_).Append("etc").Append("frecon")));
  ASSERT_TRUE(
      base::WriteFile(base::FilePath(test_root_).Append("etc/frecon/scale"),
                      frecon_scale_factor));
  ASSERT_TRUE(
      base::WriteFile(base::FilePath(test_root_).Append("etc/frecon/size"),
                      frecon_canvas_size));

  screens_.GetFreconConstants();
  EXPECT_EQ(screens_.frecon_scale_factor_, 2);
  EXPECT_EQ(screens_.frecon_canvas_size_, 1100);
}

TEST_F(ScreensTest, GetFreconConstNoInt) {
  // Set  the values to be incorrectly formatted.
  std::string frecon_scale_factor = " not a scale ";
  std::string frecon_canvas_size = " not a number ";
  ASSERT_TRUE(CreateDirectory(
      base::FilePath(test_root_).Append("etc").Append("frecon")));
  ASSERT_TRUE(
      base::WriteFile(base::FilePath(test_root_).Append("etc/frecon/scale"),
                      frecon_scale_factor));
  ASSERT_TRUE(
      base::WriteFile(base::FilePath(test_root_).Append("etc/frecon/size"),
                      frecon_canvas_size));

  screens_.GetFreconConstants();
  // Keeps default value.
  EXPECT_EQ(screens_.frecon_scale_factor_, kFreconScalingFactor);
  EXPECT_EQ(screens_.frecon_canvas_size_, kCanvasSize);
}

TEST_F(ScreensTest, GetFreconConstNoFile) {
  // Should keep the default value.
  screens_.GetFreconConstants();
  EXPECT_EQ(screens_.frecon_scale_factor_, kFreconScalingFactor);
  EXPECT_EQ(screens_.frecon_canvas_size_, kCanvasSize);
}

TEST_F(ScreensTest, MapRegionToKeyboardNotDict) {
  std::string not_dict =
      "{ au : { region_code :  au ,  confirmed : true, "
      " description :  Australia ,  keyboards : [ xkb:us::eng ], "
      " time_zones : [ Australia/Sydney ],  locales : [ en-AU ], "
      " keyboard_mechanical_layout ";
  ASSERT_TRUE(base::WriteFile(
      base::FilePath(test_root_).Append("usr/share/misc/cros-regions.json"),
      not_dict));
  std::string keyboard;
  EXPECT_FALSE(screens_.MapRegionToKeyboard(&keyboard));
  EXPECT_TRUE(keyboard.empty());
}

TEST_F(ScreensTest, MapRegionToKeyboardNoKeyboard) {
  ASSERT_TRUE(base::WriteFile(
      base::FilePath(test_root_).Append("usr/share/misc/cros-regions.json"),
      kCrosJsonSnippet));

  // Find keyboard for region. "us" dict entry does not have a keyboard value.
  screens_.vpd_region_ = "us";
  std::string keyboard;
  EXPECT_FALSE(screens_.MapRegionToKeyboard(&keyboard));
  EXPECT_TRUE(keyboard.empty());

  // Given Vpd region not available at all.
  screens_.vpd_region_ = "fr";
  EXPECT_FALSE(screens_.MapRegionToKeyboard(&keyboard));
  EXPECT_TRUE(keyboard.empty());
}

TEST_F(ScreensTest, MapRegionToKeyboardBadKeyboardFormat) {
  ASSERT_TRUE(base::WriteFile(
      base::FilePath(test_root_).Append("usr/share/misc/cros-regions.json"),
      kCrosJsonSnippet));

  // Find keyboard for region. "he" dict entry does not have a correctly
  // formatted keyboard value.
  screens_.vpd_region_ = "he";
  std::string keyboard;
  EXPECT_FALSE(screens_.MapRegionToKeyboard(&keyboard));
  EXPECT_TRUE(keyboard.empty());
}

TEST_F(ScreensTest, MapRegionToKeyboard) {
  ASSERT_TRUE(base::WriteFile(
      base::FilePath(test_root_).Append("usr/share/misc/cros-regions.json"),
      kCrosJsonSnippet));

  // Find keyboard for region.
  screens_.vpd_region_ = "au";
  std::string keyboard;
  EXPECT_TRUE(screens_.MapRegionToKeyboard(&keyboard));
  EXPECT_EQ(keyboard, "us");

  // Multiple keyboards available.
  screens_.vpd_region_ = "be";
  EXPECT_TRUE(screens_.MapRegionToKeyboard(&keyboard));
  EXPECT_EQ(keyboard, "be");
}

class MockScreens : public Screens {
 public:
  MockScreens() : Screens(nullptr) {}
  MOCK_METHOD(bool,
              ShowBox,
              (int offset_x,
               int offset_y,
               int size_x,
               int size_y,
               const std::string& color));
  MOCK_METHOD(bool,
              ShowImage,
              (const base::FilePath& image_name, int offset_x, int offset_y));
  MOCK_METHOD(bool,
              ShowMessage,
              (const std::string& message_token, int offset_x, int offset_y));
  MOCK_METHOD(bool,
              ShowText,
              (const std::string& text,
               int glyph_offset_h,
               int glyph_offset_v,
               const std::string& color));
  MOCK_METHOD(void, ShowNewScreen, ());
  MOCK_METHOD(void, LanguageMenuOnSelect, ());
  MOCK_METHOD(void, GetPassword, ());
  MOCK_METHOD(void, OnLocaleChange, ());
  MOCK_METHOD(void, ShowMiniOsCompleteScreen, ());
};

class ScreensTestMocks : public ::testing::Test {
 public:
  void SetUp() override {
    base::ScopedTempDir temp_dir_;
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    screens_path_ = base::FilePath(temp_dir_.GetPath()).Append(kScreens);
    brillo::TouchFile(screens_path_.Append("en-US").Append("constants.sh"));
    mock_screens_.SetRootForTest(temp_dir_.GetPath().value());
    mock_screens_.InitForTest();
  }

 protected:
  base::FilePath screens_path_;
  MockScreens mock_screens_;
};

TEST_F(ScreensTestMocks, ShowButtonFocused) {
  const int offset_y = 50;
  const int inner_width = 45;
  std::string message = "btn_enter";

  // Clear the button area.
  EXPECT_CALL(mock_screens_, ShowBox(_, offset_y, _, _, kMenuBlack))
      .WillRepeatedly(testing::Return(true));

  // Show button.
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("btn_bg_left_focused.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("btn_bg_right_focused.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowBox(_, offset_y, inner_width, _, kMenuBlue))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowMessage(message + "_focused", _, offset_y))
      .WillOnce(testing::Return(true));

  brillo::TouchFile(
      screens_path_.Append("en-US").Append(message + "_focused.png"));
  mock_screens_.ShowButton(message, offset_y, /*focus=*/true, inner_width,
                           false);
}

TEST_F(ScreensTestMocks, ShowButton) {
  const int offset_y = 50;
  const int inner_width = 45;
  const std::string message = "btn_enter";

  // Clear the button area.
  EXPECT_CALL(mock_screens_, ShowBox(_, offset_y, _, _, kMenuBlack))
      .WillRepeatedly(testing::Return(true));

  // Show button.
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("btn_bg_left.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("btn_bg_right.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowMessage(message, _, offset_y))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowBox(_, _, _, _, kMenuButtonFrameGrey))
      .Times(2)
      .WillRepeatedly(testing::Return(true));

  brillo::TouchFile(screens_path_.Append("en-US").Append(message + ".png"));
  mock_screens_.ShowButton(message, offset_y, /*focus=*/false, inner_width,
                           false);
}

TEST_F(ScreensTestMocks, ShowButtonTextFocused) {
  const int offset_y = 50;
  const int inner_width = 45;
  std::string text_message = "enter";

  // Clear the button area.
  EXPECT_CALL(mock_screens_, ShowBox(_, offset_y, _, _, kMenuBlack))
      .WillRepeatedly(testing::Return(true));

  // Show button.
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("btn_bg_left_focused.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("btn_bg_right_focused.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowBox(_, offset_y, inner_width, _, kMenuBlue))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowText(text_message, _, _, "black"))
      .WillOnce(testing::Return(true));

  mock_screens_.ShowButton(text_message, offset_y, /*focus=*/true, inner_width,
                           true);
}

TEST_F(ScreensTestMocks, ShowButtonText) {
  const int offset_y = 50;
  const int inner_width = 45;
  const std::string text_message = "btn_enter";

  // Clear the button area.
  EXPECT_CALL(mock_screens_, ShowBox(_, offset_y, _, _, kMenuBlack))
      .WillRepeatedly(testing::Return(true));

  // Show button.
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("btn_bg_left.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("btn_bg_right.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowText(text_message, _, _, "white"))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowBox(_, _, _, _, kMenuButtonFrameGrey))
      .Times(2)
      .WillRepeatedly(testing::Return(true));

  mock_screens_.ShowButton(text_message, offset_y, /*focus=*/false, inner_width,
                           true);
}

TEST_F(ScreensTestMocks, ShowStepper) {
  const std::string step1 = "done";
  const std::string step2 = "2";
  const std::string step3 = "error";

  // Create icons.
  brillo::TouchFile(screens_path_.Append("ic_" + step1 + ".png"));
  brillo::TouchFile(screens_path_.Append("ic_" + step2 + ".png"));
  brillo::TouchFile(screens_path_.Append("ic_" + step3 + ".png"));

  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("ic_" + step1 + ".png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("ic_" + step2 + ".png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("ic_" + step3 + ".png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowBox(_, _, _, _, kMenuGrey))
      .Times(2)
      .WillRepeatedly(testing::Return(true));

  mock_screens_.ShowStepper({step1, step2, step3});
}

TEST_F(ScreensTestMocks, ShowStepperError) {
  brillo::TouchFile(screens_path_.Append("ic_done.png"));

  const std::string step1 = "done";
  const std::string step2 = "2";
  const std::string step3 = "error";

  // Stepper icons not found. Default to done.
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("ic_done.png"), _, _))
      .Times(3)
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowBox(_, _, _, _, kMenuGrey))
      .Times(2)
      .WillRepeatedly(testing::Return(true));
  mock_screens_.ShowStepper({step1, step2, step3});
}

TEST_F(ScreensTestMocks, ShowLanguageMenu) {
  EXPECT_CALL(
      mock_screens_,
      ShowImage(screens_path_.Append("language_menu_bg_focused.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("ic_language-globe.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowImage(screens_path_.Append("ic_dropdown.png"), _, _))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowMessage("language_folded", _, _))
      .WillOnce(testing::Return(true));

  mock_screens_.ShowLanguageMenu(/* focus=*/true);
}

TEST_F(ScreensTestMocks, ShowFooter) {
  // Show left and right footer components.
  EXPECT_CALL(mock_screens_,
              ShowMessage(testing::StartsWith("footer_left"), _, _))
      .Times(3)
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(mock_screens_,
              ShowMessage(testing::StartsWith("footer_right"), _, _))
      .Times(2)
      .WillRepeatedly(testing::Return(true));

  // Show key icons and QR code and HWID text glyphs.
  EXPECT_CALL(mock_screens_, ShowImage(_, _, _))
      .Times(testing::AnyNumber())
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(mock_screens_, ShowBox(_, _, _, _, kMenuGrey))
      .WillOnce(testing::Return(true));

  mock_screens_.ShowFooter();
}

TEST_F(ScreensTestMocks, OnKeyPress) {
  std::vector<int> keys = {kKeyDown, kKeyEnter, kKeyUp};
  mock_screens_.SetIndexForTest(1);
  // Index changes up one after both press and release are recorded in
  // `key_state_`. `SwitchScreen` is called for every valid key release.

  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.OnKeyPress(0, kKeyDown, false);
  EXPECT_EQ(mock_screens_.GetIndexForTest(), 1);
  mock_screens_.OnKeyPress(0, kKeyDown, true);
  EXPECT_EQ(mock_screens_.GetIndexForTest(), 2);

  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.OnKeyPress(0, kKeyEnter, false);
  mock_screens_.OnKeyPress(0, kKeyEnter, true);
}

TEST_F(ScreensTestMocks, ScreenFlowLanguage) {
  // Test making a selection on the language screen and then returning back to
  // the previous screen.
  // Index 0 on a normal screen is the language dropdown button.
  mock_screens_.SetIndexForTest(0);
  mock_screens_.SetScreenForTest(0);

  // Calls language menu.
  EXPECT_CALL(mock_screens_, LanguageMenuOnSelect());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(4, mock_screens_.GetScreenForTest());

  // Select language from menu, make changes, and return to previous screen.
  EXPECT_CALL(mock_screens_, OnLocaleChange());
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(0, mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, ScreenFlowForward) {
  // Test the screen flow forward starting from the welcome screen.
  mock_screens_.SetIndexForTest(1);
  mock_screens_.SetScreenForTest(0);
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(/*enter=*/false);

  // Screen has not changed since enter is false.
  EXPECT_EQ(0, mock_screens_.GetScreenForTest());

  // Moves to next screen in flow. kDropDownScreen.
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(1, mock_screens_.GetScreenForTest());

  // Enter goes to kExpandedDropDownScreen.
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(2, mock_screens_.GetScreenForTest());

  // Enter goes to kPasswordScreen.
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  EXPECT_CALL(mock_screens_, GetPassword());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(3, mock_screens_.GetScreenForTest());

  // Enter finishes the flow with downloading and complete screens.
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(5, mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, ScreenBackward) {
  // Test the screen flow backward starting from the password screen.
  mock_screens_.SetIndexForTest(2);
  // Start at password screen.
  mock_screens_.SetScreenForTest(3);

  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  // Moves back to kDropDownScreen.
  EXPECT_EQ(1, mock_screens_.GetScreenForTest());

  // Enter goes back to kWelcomeScreen.
  mock_screens_.SetIndexForTest(2);
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(0, mock_screens_.GetScreenForTest());

  // Cannot go further back from kWelcomeScreen.
  mock_screens_.SetIndexForTest(2);
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.SwitchScreen(true);
  EXPECT_EQ(0, mock_screens_.GetScreenForTest());
}

TEST_F(ScreensTestMocks, UpdateEngineError) {
  mock_screens_.display_update_engine_state_ = true;
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::ERROR);

  // Show download error.
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.OnProgressChanged(status);
  EXPECT_FALSE(mock_screens_.display_update_engine_state_);
}

TEST_F(ScreensTestMocks, UpdateEngineProgressComplete) {
  mock_screens_.display_update_engine_state_ = true;
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::UPDATED_NEED_REBOOT);

  EXPECT_CALL(mock_screens_, ShowMiniOsCompleteScreen());
  mock_screens_.OnProgressChanged(status);
  // Freeze UI, nothing left to do but reboot.
  EXPECT_FALSE(mock_screens_.display_update_engine_state_);
}

TEST_F(ScreensTestMocks, IdleError) {
  mock_screens_.display_update_engine_state_ = true;
  update_engine::StatusResult status;
  status.set_current_operation(update_engine::Operation::FINALIZING);
  mock_screens_.OnProgressChanged(status);

  // If it changes to `IDLE` from an incorrect state it is an error.
  status.set_current_operation(update_engine::Operation::IDLE);
  EXPECT_CALL(mock_screens_, ShowNewScreen());
  mock_screens_.OnProgressChanged(status);
  EXPECT_FALSE(mock_screens_.display_update_engine_state_);
}

}  // namespace screens
