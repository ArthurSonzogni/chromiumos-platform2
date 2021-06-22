// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/screens.h"

#include "minios/utils.h"

namespace minios {
namespace {
// Dimension Constants for Logging
const int kLogCharPerLine = 111;
const int kLogLinesPerPage = 20;
const int kLogAreaWidth = kMonospaceGlyphWidth * kLogCharPerLine;
const int kLogAreaHeight = kMonospaceGlyphHeight * kLogLinesPerPage;
// y-coord of the upper edge of the log area, 16px below title.
const int kLogAreaY = 196;
}  // namespace

// TODO(vyshu): Delete this screen once screens.h is deprecated.

void Screens::ShowMiniOsLogScreen() {
  MessageBaseScreen();
  ShowMessage("title_message_log",
              -frecon_canvas_size_ / 2 + kDefaultMessageWidth / 2,
              -frecon_canvas_size_ / 2 + 162);
  UpdateLogScreenButtons();
  UpdateLogArea();
}

void Screens::UpdateLogScreenButtons() {
  ShowLanguageMenu(index_ == 0);
  auto y_offset = -frecon_canvas_size_ / 2 + kLogAreaY + kLogAreaHeight + 16 +
                  kButtonHeight / 2;
  auto y_offset_step = kButtonHeight + kButtonMargin;
  ShowButton("btn_page_up", y_offset, index_ == 1, default_button_width_,
             false);
  ShowButton("btn_page_down", y_offset + y_offset_step, index_ == 2,
             default_button_width_, false);
  ShowButton("btn_back", y_offset + 2 * y_offset_step, index_ == 3,
             default_button_width_, false);
}

void Screens::UpdateLogArea() {
  ShowImage(screens_path_.Append("log_area_border_large.png"),
            -frecon_canvas_size_ / 2 + (kLogAreaWidth + 10) / 2,
            -frecon_canvas_size_ / 2 + kLogAreaY + kLogAreaHeight / 2);

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
  ShowText(content, -frecon_canvas_size_ / 2,
           -frecon_canvas_size_ / 2 + kLogAreaY + kMonospaceGlyphHeight / 2,
           "white");
}

}  // namespace minios
