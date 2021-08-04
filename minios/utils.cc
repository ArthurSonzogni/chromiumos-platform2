// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/utils.h"

#include <cstdio>
#include <tuple>
#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>

namespace minios {

std::tuple<bool, std::string> ReadFileContentWithinRange(
    const base::FilePath& file_path,
    int64_t start_offset,
    int64_t end_offset,
    int max_columns) {
  base::File f(file_path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!f.IsValid()) {
    PLOG(ERROR) << "Failed to open file " << file_path.value();
    return {false, {}};
  }

  if (f.Seek(base::File::Whence::FROM_BEGIN, start_offset) != start_offset) {
    PLOG(ERROR) << "Failed to seek file " << file_path.value() << " at offset "
                << start_offset;
    return {false, {}};
  }

  int64_t bytes_to_read = end_offset - start_offset;
  std::string content;
  content.reserve(bytes_to_read);

  int current_col = 0;
  while (bytes_to_read-- > 0) {
    char c;
    switch (f.ReadAtCurrentPos(&c, 1)) {
      case -1:
        PLOG(ERROR) << "Failed to read file " << file_path.value();
        return {false, {}};
      case 0:
        // Equivalent of EOF.
        return {true, content};
      default:
        break;
    }
    if (c == '\n') {
      if (content.empty() || content.back() != '\n')
        content.push_back(c);
      current_col = 0;
      continue;
    }
    if (current_col < max_columns) {
      content.push_back(c);
      if (++current_col >= max_columns) {
        content.push_back('\n');
        current_col = 0;
      }
    }
  }
  return {true, content};
}

std::tuple<bool, std::string, int64_t> ReadFileContent(
    const base::FilePath& file_path,
    int64_t offset,
    int num_lines,
    int num_cols) {
  base::File f(file_path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!f.IsValid())
    return {false, {}, 0};

  if (f.Seek(base::File::Whence::FROM_BEGIN, offset) == -1)
    return {false, {}, 0};

  char c;
  std::string content;
  content.reserve(num_lines * num_cols);
  int64_t bytes_read = 0;
  int current_line = 0, current_col = 0, read_buffer_lines = 0;
  while (f.ReadAtCurrentPos(&c, 1) > 0 && read_buffer_lines < num_lines) {
    ++bytes_read;
    if (c == '\n') {
      // Skip double newlining.
      if (content.back() != '\n') {
        content.push_back(c);
        ++read_buffer_lines;
      }
      current_col = 0;
      ++current_line;
      continue;
    }
    if (current_col < num_cols) {
      content.push_back(c);
      if (++current_col >= num_cols) {
        content.push_back('\n');
        current_col = 0;
        ++read_buffer_lines;
      }
    }
  }
  return {true, content, bytes_read};
}

std::string GetVpdRegion(const base::FilePath& root,
                         ProcessManagerInterface* process_manager) {
  std::string vpd_region;
  if (ReadFileToString(root.Append("sys/firmware/vpd/ro/region"),
                       &vpd_region)) {
    return vpd_region;
  }
  LOG(WARNING) << "Could not read vpd region from file. Trying commandline.";
  int exit_code = 0;
  std::string error;
  if (process_manager->RunCommandWithOutput({"/bin/vpd", "-g", "region"},
                                            &exit_code, &vpd_region, &error) &&
      !exit_code) {
    return vpd_region;
  }
  PLOG(WARNING) << "Error getting vpd -g region. Exit code " << exit_code
                << " with error " << error << ". Defaulting to 'us'. ";
  return "us";
}

}  // namespace minios
