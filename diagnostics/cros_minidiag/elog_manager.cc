// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_minidiag/elog_manager.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace cros_minidiag {

namespace {
// The index of [type] field in a valid elog event.
constexpr int kTypeIndex = 2;
constexpr int kSubTypeIndex = 3;

// The format of a legacy MiniDiag launch event:
// idx | time | Diagnostics Mode | Launch Diagnostics
constexpr const char kDataLaunchDiagnostics[] = "Launch Diagnostics";
// The format of a MiniDiag launch event:
// idx | time | Firmware vboot info | boot_mode=Diagnostic | fw_tried=...
constexpr const char kDataBootModeDiagnostic[] = "boot_mode=Diagnostic";
}  // namespace

ElogEvent::ElogEvent(const base::StringPiece& event_string)
    : data_(base::SplitString(event_string,
                              "|",
                              base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {}

ElogEvent::~ElogEvent() = default;

std::optional<std::string> ElogEvent::GetColumn(int idx) const {
  if (data_.size() < idx + 1) {
    return std::nullopt;
  }
  return data_[idx];
}

std::optional<std::string> ElogEvent::GetType() const {
  const auto result = GetColumn(kTypeIndex);
  LOG_IF(ERROR, !result) << "Invalid event. Too few columns: " << data_.size();
  return result;
}

std::optional<std::string> ElogEvent::GetSubType() const {
  return GetColumn(kSubTypeIndex);
}

ElogManager::ElogManager(const std::string& elog_string,
                         const std::string& previous_last_line)
    : ElogManager(elog_string, previous_last_line, &default_minidiag_metrics_) {
}

ElogManager::ElogManager(const std::string& elog_string,
                         const std::string& previous_last_line,
                         MiniDiagMetrics* minidiag_metrics)
    : metrics_(minidiag_metrics) {
  base::StringPiece last_line_piece;

  // We only want to store the new events which appear after
  // `previous_last_line`. If `previous_last_line` is empty or the elog_string
  // does not contains it, store the full events instead.
  bool is_new_event = false;
  if (previous_last_line.empty() ||
      elog_string.find(previous_last_line) == std::string::npos) {
    is_new_event = true;
  }

  for (const auto& line :
       base::SplitStringPiece(elog_string, "\n", base::TRIM_WHITESPACE,
                              base::SPLIT_WANT_NONEMPTY)) {
    if (is_new_event) {
      elog_events_.emplace_back(std::forward<const base::StringPiece&>(line));
    } else if (line.compare(previous_last_line) == 0) {
      is_new_event = true;
    }
    last_line_piece = line;
  }
  last_line_ = std::string(last_line_piece);
}

ElogManager::~ElogManager() = default;

int ElogManager::GetEventNum() const {
  return elog_events_.size();
}

void ElogManager::ReportMiniDiagLaunch() const {
  int count = 0;
  for (const auto& elog_event : elog_events_) {
    const auto subtype = elog_event.GetSubType();
    if (subtype && (*subtype == kDataLaunchDiagnostics ||
                    *subtype == kDataBootModeDiagnostic)) {
      count++;
    }
  }
  LOG(INFO) << "Record Launch Count: " << count;
  metrics_->RecordLaunch(count);
}

}  // namespace cros_minidiag
