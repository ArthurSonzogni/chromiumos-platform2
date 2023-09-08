// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/cli/file_pattern.h"

#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

namespace lorgnette::cli {

namespace {

std::string EscapeScannerName(const std::string& scanner_name) {
  std::string escaped;
  for (char c : scanner_name) {
    if (isalnum(c)) {
      escaped += c;
    } else {
      escaped += '_';
    }
  }
  return escaped;
}

}  // namespace

base::FilePath ExpandPattern(const std::string& pattern,
                             size_t page,
                             const std::string& scanner_name,
                             const std::string& extension) {
  std::string expanded_path = pattern;
  base::ReplaceFirstSubstringAfterOffset(&expanded_path, 0, "%n",
                                         base::StringPrintf("%zu", page));
  base::ReplaceFirstSubstringAfterOffset(&expanded_path, 0, "%s",
                                         EscapeScannerName(scanner_name));
  base::ReplaceFirstSubstringAfterOffset(&expanded_path, 0, "%e", extension);
  base::FilePath output_path = base::FilePath(expanded_path);
  if (page > 1 && pattern.find("%n") == std::string::npos) {
    output_path =
        output_path.InsertBeforeExtension(base::StringPrintf("_page%zu", page));
  }
  return output_path;
}

}  // namespace lorgnette::cli
