// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_CLI_FILE_PATTERN_H_
#define LORGNETTE_CLI_FILE_PATTERN_H_

#include <string>

#include <base/files/file_path.h>

namespace lorgnette::cli {

base::FilePath ExpandPattern(const std::string& pattern,
                             size_t page,
                             const std::string& scanner_name,
                             const std::string& extension);

}  // namespace lorgnette::cli

#endif  // LORGNETTE_CLI_FILE_PATTERN_H_
