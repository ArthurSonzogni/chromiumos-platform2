// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens/screen_log.h"

#include <utility>

#include "minios/draw_utils.h"
#include "minios/utils.h"

namespace minios {

namespace {
const int kLogCharPerLine = 111;
const int kLogLinesPerPage = 20;
// Logging constants.
const int kLogAreaWidth = kMonospaceGlyphWidth * kLogCharPerLine;
const int kLogAreaHeight = kMonospaceGlyphHeight * kLogLinesPerPage;
// y-coord of the upper edge of the log area, 16px below title.
const int kLogAreaY = 196;

const char kLogPath[] = "/var/log/messages";
}  // namespace

ScreenLog::ScreenLog(std::shared_ptr<DrawInterface> draw_utils,
                     ScreenControllerInterface* screen_controller)
    : ScreenBase(
          /*button_count=*/4, /*index_=*/1, draw_utils, screen_controller),
      log_path_(base::FilePath(kLogPath)),
      log_offset_idx_(0),
      log_offsets_({0}) {}

void ScreenLog::Show() {
  draw_utils_->MessageBaseScreen();
  int frecon_size = draw_utils_->GetFreconCanvasSize();
  const int kXOffset = (-frecon_size / 2) + (720 / 2);
  draw_utils_->ShowMessage("title_message_log", kXOffset,
                           -frecon_size / 2 + 162);
  ShowButtons();
  UpdateLogArea();
}

void ScreenLog::ShowButtons() {
  draw_utils_->ShowLanguageMenu(index_ == 0);
  auto y_offset = -draw_utils_->GetFreconCanvasSize() / 2 + kLogAreaY +
                  kLogAreaHeight + 16 + kButtonHeight / 2;
  auto y_offset_step = kButtonHeight + kButtonMargin;
  int default_btn_width = draw_utils_->GetDefaultButtonWidth();
  draw_utils_->ShowButton("btn_page_up", y_offset, index_ == 1,
                          default_btn_width, false);
  draw_utils_->ShowButton("btn_page_down", y_offset + y_offset_step,
                          index_ == 2, default_btn_width, false);
  draw_utils_->ShowButton("btn_back", y_offset + 2 * y_offset_step, index_ == 3,
                          default_btn_width, false);
}

void ScreenLog::UpdateLogArea() {
  int frecon_size = draw_utils_->GetFreconCanvasSize();
  draw_utils_->ShowImage(
      draw_utils_->GetScreenPath().Append("log_area_border_large.png"),
      -frecon_size / 2 + (kLogAreaWidth + 10) / 2,
      -frecon_size / 2 + kLogAreaY + kLogAreaHeight / 2);

  std::string content;
  // If the offsets into the file are already calculated, use the start and end
  // byte offsets into the file to quickly index.
  if (log_offset_idx_ + 1 < log_offsets_.size()) {
    auto start_offset = log_offsets_[log_offset_idx_],
         end_offset = log_offsets_[log_offset_idx_ + 1];
    auto [success, content_local] = ReadFileContentWithinRange(
        log_path_, start_offset, end_offset, kLogCharPerLine);
    content = std::move(content_local);
    if (!success) {
      PLOG(ERROR) << "Failed to read content from " << log_path_.value()
                  << " between offsets " << start_offset << " and "
                  << end_offset;
    }
  } else {
    // Otherwise, the new end offset must be calculated based off the number of
    // lines and columns to read.
    auto start_offset = log_offsets_[log_offset_idx_];
    auto [success, content_local, bytes_read] = ReadFileContent(
        log_path_, start_offset, kLogLinesPerPage, kLogCharPerLine);
    content = std::move(content_local);
    if (!success) {
      PLOG(ERROR) << "Failed to read content from " << log_path_.value()
                  << " starting ad offset " << start_offset;
    } else if (bytes_read != 0) {
      log_offsets_.push_back(start_offset + bytes_read);
    }
  }
  draw_utils_->ShowText(
      content, -frecon_size / 2,
      -frecon_size / 2 + kLogAreaY + kMonospaceGlyphHeight / 2, "white");
}

void ScreenLog::OnKeyPress(int key_changed) {
  bool enter = false;
  UpdateButtonsIndex(key_changed, &enter);
  if (enter) {
    switch (index_) {
      case 0:
        screen_controller_->SwitchLocale(this);
        break;
      case 1:
        if (log_offset_idx_ > 0) {
          --log_offset_idx_;
          UpdateLogArea();
        }
        break;
      case 2:
        if (log_offset_idx_ < log_offsets_.size() - 1) {
          ++log_offset_idx_;
          UpdateLogArea();
        }
        break;
      case 3:
        // Back to debug options screen.
        screen_controller_->OnBackward(this);
        break;
      default:
        LOG(FATAL) << "Index " << index_ << " is not valid.";
    }
  } else {
    ShowButtons();
  }
}

void ScreenLog::Reset() {}

ScreenType ScreenLog::GetType() {
  return ScreenType::kLogScreen;
}

std::string ScreenLog::GetName() {
  return "ScreenLog";
}

}  // namespace minios
