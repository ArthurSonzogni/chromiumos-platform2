// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_minidiag/elog_manager.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace cros_minidiag {

namespace {
// The index of [type] field in a valid elog event.
const int kTypeIndex = 2;
}  // namespace

ElogEvent::ElogEvent(const base::StringPiece& event_string)
    : data_(base::SplitString(event_string,
                              "|",
                              base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {}

ElogEvent::~ElogEvent() = default;

std::string ElogEvent::GetType() const {
  if (data_.size() < kTypeIndex + 1) {
    LOG(ERROR) << "Invalid event. Too few columns: " << data_.size();
    return "";
  }
  return data_[kTypeIndex];
}

ElogManager::ElogManager(const std::string& elog_string) {
  base::StringPiece last_line_piece;

  for (const auto& line :
       base::SplitStringPiece(elog_string, "\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    elog_events_.emplace_back(std::forward<const base::StringPiece&>(line));
    last_line_piece = line;
  }
  last_line_ = std::string(last_line_piece);
  LOG(INFO) << "Parse elogtool output with last line: " << last_line_;
}

ElogManager::~ElogManager() = default;

}  // namespace cros_minidiag
