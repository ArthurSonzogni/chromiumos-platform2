// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_MINIDIAG_ELOG_MANAGER_H_
#define DIAGNOSTICS_CROS_MINIDIAG_ELOG_MANAGER_H_

#include <string>
#include <vector>

#include <base/strings/string_piece.h>

namespace cros_minidiag {

// A valid line of event would look like:
// [idx] | [date] | [type] | [data0] | [data1] ...
// Where [idx], [data], [type] are required field and [data*] are optional.
class ElogEvent {
 public:
  explicit ElogEvent(const base::StringPiece& event_string);
  ~ElogEvent();
  // Retrieves the [type] of the event. The [type] is a mandatory field and
  // always the 3rd column in the event string.
  // Return the [type] string, or an empty string if an error occurs.
  std::string GetType() const;
  // The accessor of data_.
  const std::vector<std::string>& data() const { return data_; }

 private:
  std::vector<std::string> data_;
};

// ElogManager get the raw output generated from elogtool and parse it line by
// line.
class ElogManager {
 public:
  explicit ElogManager(const std::string& elog_string);
  ~ElogManager();
  // The accessor of last_line_.
  const std::string& last_line() const { return last_line_; }

  // TODO(roccochen) Add report metrics related helper

 private:
  std::string last_line_;
  std::vector<ElogEvent> elog_events_;
};

}  // namespace cros_minidiag
#endif  // DIAGNOSTICS_CROS_MINIDIAG_ELOG_MANAGER_H_
