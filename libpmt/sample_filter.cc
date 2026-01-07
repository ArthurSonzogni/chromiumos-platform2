// Copyright 2026 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libpmt/sample_filter.h"

#include <optional>
#include <string>
#include <vector>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>

namespace pmt {

using std::string;
using std::vector;

vector<Filter> ParseFilters(const vector<string>& filter_strs) {
  vector<Filter> filters;
  for (const auto& s : filter_strs) {
    Filter f;
    if (s.empty()) {
      continue;
    }
    // Filter format is [/<guid>[/<sample-group>/]][sample-id].
    // Wildcards are supported by "*".
    if (s == "/" || s.find("//") != string::npos) {
      LOG(WARNING) << "Skipping invalid filter: " << s;
      continue;
    }
    if (s[0] == '/') {
      auto parts = base::SplitString(s, "/", base::KEEP_WHITESPACE,
                                     base::SPLIT_WANT_ALL);
      // parts[0] is empty.
      if (parts.size() > 1 && !parts[1].empty() && parts[1] != "*") {
        unsigned int guid;
        if (base::HexStringToUInt(parts[1], &guid)) {
          f.guid = guid;
        }
      }
      if (parts.size() > 2 && !parts[2].empty() && parts[2] != "*") {
        f.group = parts[2];
      }
      if (parts.size() > 3 && !parts[3].empty() && parts[3] != "*") {
        f.sample = parts[3];
      }
    } else {
      if (s != "*") {
        f.sample = s;
      }
    }
    filters.push_back(f);
  }
  return filters;
}

bool IsSampleSelected(const vector<Filter>& filters,
                      Guid guid,
                      const string& group,
                      const string& sample) {
  if (filters.empty()) {
    return true;
  }
  for (const auto& f : filters) {
    if (f.guid && *f.guid != guid) {
      continue;
    }
    if (f.group && *f.group != group) {
      continue;
    }
    if (f.sample && *f.sample != sample) {
      continue;
    }
    return true;
  }
  return false;
}

}  // namespace pmt
