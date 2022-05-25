// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include <base/check.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

#include "runtime_probe/utils/file_utils.h"

using base::FilePath;
using base::ReadFileToStringWithMaxSize;
using base::TrimWhitespaceASCII;
using base::Value;
using base::TrimPositions::TRIM_ALL;
using std::pair;
using std::string;
using std::vector;

namespace {

constexpr int kReadFileMaxSize = 1024;
constexpr int kGlobIterateCountLimit = 32768;

bool ReadFile(const base::FilePath& dir_path,
              base::StringPiece file_name,
              string* out) {
  if (base::FilePath{file_name}.IsAbsolute()) {
    LOG(ERROR) << "file_name " << file_name << " is absolute";
    return false;
  }
  const auto file_path = dir_path.Append(file_name);
  if (!base::PathExists(file_path))
    return false;
  if (!ReadFileToStringWithMaxSize(file_path, out, kReadFileMaxSize)) {
    LOG(ERROR) << file_path.value() << " exists, but we can't read it";
    return false;
  }
  TrimWhitespaceASCII(*out, TRIM_ALL, out);
  return true;
}

bool HasPathWildcard(const string& path) {
  const std::string wildcard = "*?[";
  for (const auto& c : path) {
    if (wildcard.find(c) != string::npos)
      return true;
  }
  return false;
}

std::vector<base::FilePath> GlobInternal(
    const base::FilePath& root,
    const std::vector<std::string>& patterns,
    int idx,
    int* iterate_counter) {
  DCHECK(iterate_counter);
  if (++(*iterate_counter) >= kGlobIterateCountLimit)
    return {};

  if (idx == patterns.size()) {
    if (PathExists(root))
      return {root};
    return {};
  }
  const auto& pattern = patterns[idx];
  if (!HasPathWildcard(pattern)) {
    return GlobInternal(root.Append(pattern), patterns, idx + 1,
                        iterate_counter);
  }
  std::vector<base::FilePath> res;
  base::FileEnumerator it(root, false,
                          base::FileEnumerator::SHOW_SYM_LINKS |
                              base::FileEnumerator::FILES |
                              base::FileEnumerator::DIRECTORIES,
                          pattern);
  for (auto path = it.Next(); !path.empty(); path = it.Next()) {
    auto sub_res = GlobInternal(path, patterns, idx + 1, iterate_counter);
    std::move(sub_res.begin(), sub_res.end(), std::back_inserter(res));
  }
  return res;
}

}  // namespace

namespace runtime_probe {
namespace internal {

bool ReadFileToDict(const FilePath& dir_path,
                    base::StringPiece key,
                    bool log_error,
                    Value& result) {
  return ReadFileToDict(dir_path, {key, key}, log_error, result);
}

bool ReadFileToDict(const FilePath& dir_path,
                    const pair<base::StringPiece, base::StringPiece>& key,
                    bool log_error,
                    Value& result) {
  base::StringPiece file_name = key.second;
  string content;
  if (!ReadFile(dir_path, file_name, &content)) {
    LOG_IF(ERROR, log_error) << "file \"" << file_name << "\" is required.";
    return false;
  }
  base::StringPiece key_name = key.first;
  result.SetStringKey(key_name, content);
  return true;
}

}  // namespace internal

std::vector<base::FilePath> Glob(const base::FilePath& pattern) {
  std::vector<std::string> components = pattern.GetComponents();
  int iterate_counter = 0;
  auto res = GlobInternal(base::FilePath(components[0]), components, 1,
                          &iterate_counter);
  if (iterate_counter > kGlobIterateCountLimit) {
    LOG(ERROR) << "Glob iterate count reached the limit "
               << kGlobIterateCountLimit
               << " with the input: " << pattern.value();
  }
  return res;
}

std::vector<base::FilePath> Glob(const std::string& pattern) {
  return Glob(base::FilePath(pattern));
}

}  // namespace runtime_probe
