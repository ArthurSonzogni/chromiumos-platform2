// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
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

// Get string to be used as key name in |MapFilesToDict|.
const string& GetKeyName(const string& key) {
  return key;
}

const string& GetKeyName(const pair<string, string>& key) {
  return key.first;
}

// Get string to be used as file name in |MapFilesToDict|.
const string& GetFileName(const string& key) {
  return key;
}

const string& GetFileName(const pair<string, string>& key) {
  return key.second;
}

bool ReadFile(const base::FilePath& dir_path,
              const string& file_name,
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

template <typename KeyType>
bool ReadFileToDict(const FilePath& dir_path,
                    const KeyType& key,
                    Value* result) {
  const string& file_name = GetFileName(key);
  string content;
  if (!ReadFile(dir_path, file_name, &content))
    return false;
  const string& key_name = GetKeyName(key);
  result->SetStringKey(key_name, content);
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

template <typename KeyType>
base::Optional<Value> MapFilesToDict(const FilePath& dir_path,
                                     const vector<KeyType>& keys,
                                     const vector<KeyType>& optional_keys) {
  Value result(Value::Type::DICTIONARY);

  for (const auto& key : keys) {
    if (!ReadFileToDict(dir_path, key, &result)) {
      LOG(ERROR) << "file: \"" << GetFileName(key) << "\" is required.";
      return base::nullopt;
    }
  }
  for (const auto& key : optional_keys) {
    ReadFileToDict(dir_path, key, &result);
  }
  return result;
}

// Explicit template instantiation
template base::Optional<Value> MapFilesToDict<string>(
    const FilePath& dir_path,
    const vector<string>& keys,
    const vector<string>& optional_keys);

// Explicit template instantiation
template base::Optional<Value> MapFilesToDict<pair<string, string>>(
    const FilePath& dir_path,
    const vector<pair<string, string>>& keys,
    const vector<pair<string, string>>& optional_keys);

std::vector<base::FilePath> Glob(const base::FilePath& pattern) {
  std::vector<std::string> components;
  pattern.GetComponents(&components);
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
