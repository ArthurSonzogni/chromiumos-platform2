// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_MOCK_DRAW_INTERFACE_H_
#define MINIOS_MOCK_DRAW_INTERFACE_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <gmock/gmock.h>

#include "minios/draw_interface.h"

namespace minios {

class MockDrawInterface : public DrawInterface {
 public:
  MockDrawInterface() = default;
  ~MockDrawInterface() = default;

  MockDrawInterface(const MockDrawInterface&) = delete;
  MockDrawInterface& operator=(const MockDrawInterface&) = delete;

  MOCK_METHOD(bool, Init, (), (override));
  MOCK_METHOD(bool,
              ShowBox,
              (int offset_x,
               int offset_y,
               int size_x,
               int size_y,
               const std::string& color),
              (override));
  MOCK_METHOD(bool,
              ShowImage,
              (const base::FilePath& image_name, int offset_x, int offset_y),
              (override));
  MOCK_METHOD(bool,
              ShowMessage,
              (const std::string& message_token, int offset_x, int offset_y),
              (override));
  MOCK_METHOD(bool,
              ShowText,
              (const std::string& text,
               int glyph_offset_h,
               int glyph_offset_v,
               const std::string& color),
              (override));
  MOCK_METHOD(void,
              ShowInstructions,
              (const std::string& message_token),
              (override));
  MOCK_METHOD(void,
              ShowInstructionsWithTitle,
              (const std::string& message_token),
              (override));
  MOCK_METHOD(bool, IsDetachable, (), (const, override));
  MOCK_METHOD(void,
              ShowButton,
              (const std::string& message_token,
               int offset_y,
               bool is_selected,
               int inner_width,
               bool is_text),
              (override));
  MOCK_METHOD(void,
              ShowStepper,
              (const std::vector<std::string>& steps),
              (override));
  MOCK_METHOD(void, ShowAdvancedOptionsButton, (bool focused), (override));
  MOCK_METHOD(void, ShowPowerButton, (bool focused), (override));
  MOCK_METHOD(void, MessageBaseScreen, (), (override));
  MOCK_METHOD(void, ShowLanguageDropdown, (int current_index), (override));
  MOCK_METHOD(int, FindLocaleIndex, (), (const, override));
  MOCK_METHOD(void, ShowLanguageMenu, (bool is_selected), (override));
  MOCK_METHOD(void, LocaleChange, (int selected_locale), (override));
  MOCK_METHOD(void, ShowProgressBar, (), (override));
  MOCK_METHOD(void, ShowProgressPercentage, (double progress), (override));
  MOCK_METHOD(void, ShowIndeterminateProgressBar, (), (override));
  MOCK_METHOD(void, HideIndeterminateProgressBar, (), (override));
  MOCK_METHOD(int, GetSupportedLocalesSize, (), (const, override));
  MOCK_METHOD(int, GetDefaultButtonWidth, (), (const, override));
  MOCK_METHOD(int, GetFreconCanvasSize, (), (const, override));
  MOCK_METHOD(base::FilePath, GetScreensPath, (), (const, override));
  MOCK_METHOD(bool, IsLocaleRightToLeft, (), (const, override));
  MOCK_METHOD(void,
              ShowDropDownClosed,
              (int offset_x,
               int offset_y,
               int text_x,
               const std::string& message,
               const std::string& icon_label,
               bool is_selected),
              (override));
};

}  // namespace minios

#endif  // MINIOS_MOCK_DRAW_INTERFACE_H_
