// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "parser.h"

#include "errors.h"

namespace ipp {

void SimpleParserLog::AddParserError(const AttrPath& path,
                                     ParserCode error,
                                     bool critical) {
  if (errors_.size() < max_entries_count_) {
    errors_.emplace_back(path, error);
  }
  if (critical) {
    critical_errors_.emplace_back(path, error);
  }
}

}  // namespace ipp
