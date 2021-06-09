// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_UTILS_H_
#define MINIOS_UTILS_H_

#include <string>
#include <tuple>

#include <base/files/file_path.h>

#include "minios/process_manager.h"

namespace minios {

// Reads the content of `file_path` from `start_offset` to `end_offset` with
// maximum characters per line being `max_columns` at max. If the file ends
// before reading all bytes between `start_offset` and `end_offset` it will
// return true.
// - bool: Success or failure.
// - std::string: The content read.
std::tuple<bool, std::string> ReadFileContentWithinRange(
    const base::FilePath& file_path,
    int64_t start_offset,
    int64_t end_offset,
    int num_cols);

// Reads the content of `file_path` from `offset`.
// The `num_lines` and `num_cols` is the maximum amount of lines and characters
// per line that will be read.
// The return will include:
// - bool: Success or failure.
// - std::string: The content read.
// - int64_t: The number of bytes read.
// Note: The number of bytes read can differ than the length of the content
// output in the second tuple element because the content read is formatted to
// number of lines and columns format to fit onto the requested area of
// `num_lines` * `num_cols`.
std::tuple<bool, std::string, int64_t> ReadFileContent(
    const base::FilePath& file_path,
    int64_t offset,
    int num_lines,
    int num_cols);

// Returns the VPD region from RO firmware starting at the `root` path. Returns
// "us" as the default.
std::string GetVpdRegion(const base::FilePath& root,
                         ProcessManagerInterface* process_manager);

}  // namespace minios

#endif  // MINIOS_UTILS_H__
