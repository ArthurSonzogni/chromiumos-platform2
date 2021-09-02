// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "minios/utils.h"

#include <cstdio>
#include <tuple>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>

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

bool GetCrosRegionData(ProcessManagerInterface* process_manager,
                       std::string key,
                       std::string* value) {
  int exit_code = 0;
  std::string error, xkb_keyboard;
  // Get the first item in the keyboard list for a given region.
  if (!process_manager->RunCommandWithOutput(
          {"/usr/bin/cros_region_data", "-s", key}, &exit_code, value,
          &error) ||
      exit_code) {
    LOG(ERROR) << "Could not get " << key << " region data. Exit code "
               << exit_code << " with error " << error;
    *value = "";
    return false;
  }
  return true;
}

std::string GetKeyboardLayout(ProcessManagerInterface* process_manager) {
  std::string keyboard_layout;
  if (!GetCrosRegionData(process_manager, "keyboards", &keyboard_layout)) {
    LOG(WARNING) << "Could not get region data. Defaulting to 'us'.";
    return "us";
  }
  // Get the country code from the full keyboard string (i.e xkb:us::eng).
  const auto& keyboard_parts = base::SplitString(
      keyboard_layout, ":", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (keyboard_parts.size() < 2 || keyboard_parts[1].size() < 2) {
    LOG(WARNING) << "Could not get country code from " << keyboard_layout
                 << " Defaulting to 'us'.";
    return "us";
  }
  return keyboard_parts[1];
}

}  // namespace minios
