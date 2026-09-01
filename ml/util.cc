// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/util.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/free_deleter.h>
#include <base/process/process.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace ml {

namespace {

constexpr char kDlcBasePath[] = "/run/imageloader/";
constexpr char kBuiltinModelBasePath[] = "/opt/google/chrome/ml_models/";

// Extracts the value from a value string of /proc/[pid]/status.
// Only works for value strings in the form of "value kB".
// Returns true if value could be extracted, false otherwise.
bool GetValueFromProcStatusValueStr(const std::string& value_str,
                                    size_t* value) {
  const std::vector<std::string_view> split_value_str = base::SplitStringPiece(
      value_str, " ", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  if (split_value_str.size() != 2 || split_value_str[1] != "kB") {
    return false;
  }

  return base::StringToSizeT(split_value_str[0], value);
}

// Gives resolved path using realpath(3), or empty Optional upon error. Leaves
// realpath's errno unchanged.
std::optional<base::FilePath> GetRealPath(const base::FilePath& path) {
  const std::unique_ptr<char, base::FreeDeleter> result(
      realpath(path.value().c_str(), nullptr));
  if (!result) {
    return {};
  }
  return base::FilePath(result.get());
}

// Gives and validates resolved path using realpath(3), ensuring it begins with
// `base_path_prefix`, or empty Optional upon error.
std::optional<base::FilePath> ValidateAndGetRealPathUnder(
    const base::FilePath& path, std::string_view base_path_prefix) {
  const std::optional<base::FilePath> real_path = GetRealPath(path);
  if (!real_path) {
    LOG(ERROR) << "Cannot resolve real path: " << path;
    return {};
  }
  if (!base::StartsWith(real_path->value(), base_path_prefix)) {
    LOG(ERROR) << "Path " << path << " does not reside under "
               << base_path_prefix;
    return {};
  }
  return real_path;
}

}  // namespace

bool MemoryUsage::operator==(const MemoryUsage& other) const {
  return this->VmRSSKb == other.VmRSSKb && this->VmSwapKb == other.VmSwapKb;
}

bool GetProcessMemoryUsageFromFile(MemoryUsage* memory_usage,
                                   const base::FilePath& file_path) {
  std::string status_data;
  if (!ReadFileToString(file_path, &status_data)) {
    LOG(WARNING) << "Can not open status file";
    return false;
  }

  base::StringPairs key_value_pairs;
  base::SplitStringIntoKeyValuePairs(status_data, ':', '\n', &key_value_pairs);

  bool vmrss_found = false;
  bool vmswap_found = false;

  for (auto& pair : key_value_pairs) {
    std::string& key = pair.first;
    std::string& value_str = pair.second;

    base::TrimWhitespaceASCII(key, base::TRIM_ALL, &key);

    if (key == "VmRSS") {
      if (vmrss_found) {
        return false;  // Duplicates should not happen.
      }

      base::TrimWhitespaceASCII(value_str, base::TRIM_ALL, &value_str);
      if (!GetValueFromProcStatusValueStr(value_str, &memory_usage->VmRSSKb)) {
        return false;
      }
      vmrss_found = true;
    }
    if (key == "VmSwap") {
      if (vmswap_found) {
        return false;  // Duplicates should not happen.
      }

      base::TrimWhitespaceASCII(value_str, base::TRIM_ALL, &value_str);
      if (!GetValueFromProcStatusValueStr(value_str, &memory_usage->VmSwapKb)) {
        return false;
      }
      vmswap_found = true;
    }
  }

  return vmrss_found && vmswap_found;
}

bool GetProcessMemoryUsage(MemoryUsage* memory_usage, pid_t pid) {
  const base::FilePath status_file_path = base::FilePath("/proc")
                                              .Append(base::NumberToString(pid))
                                              .Append("status");
  return GetProcessMemoryUsageFromFile(memory_usage, status_file_path);
}

bool GetProcessMemoryUsage(MemoryUsage* memory_usage) {
  return GetProcessMemoryUsage(memory_usage, base::Process::Current().Pid());
}

bool GetTotalProcessMemoryUsage(size_t* total_memory) {
  MemoryUsage memory_usage;
  if (GetProcessMemoryUsage(&memory_usage)) {
    *total_memory = memory_usage.VmRSSKb + memory_usage.VmSwapKb;
    return true;
  }
  return false;
}

// Gives and validates resolved DLC path using realpath(3), or empty Optional
// upon error. Leaves realpath's errno unchanged.
std::optional<base::FilePath> ValidateAndGetRealDlcPath(
    const base::FilePath& path) {
  return ValidateAndGetRealPathUnder(path, kDlcBasePath);
}

// Gives and validates resolved built-in model path using realpath(3), or empty
// Optional upon error. Leaves realpath's errno unchanged.
std::optional<base::FilePath> ValidateAndGetRealBuiltinModelPath(
    const base::FilePath& path) {
  return ValidateAndGetRealPathUnder(path, kBuiltinModelBasePath);
}

}  // namespace ml
