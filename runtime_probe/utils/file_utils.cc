// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <string>
#include <tuple>
#include <utility>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
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

// Get string to be used as key name in |MapFilesToDict|.
string GetKeyName(const string& key) {
  return key;
}

string GetKeyName(const pair<string, string>& key) {
  return key.first;
}

// Get string to be used as file name in |MapFilesToDict|.
string GetFileName(const string& key) {
  return key;
}

string GetFileName(const pair<string, string>& key) {
  return key.second;
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
    int idx) {
  if (idx == patterns.size()) {
    if (PathExists(root))
      return {root};
    return {};
  }
  const auto& pattern = patterns[idx];
  if (!HasPathWildcard(pattern)) {
    return GlobInternal(root.Append(pattern), patterns, idx + 1);
  }
  std::vector<base::FilePath> res;
  base::FileEnumerator it(root, false,
                          base::FileEnumerator::SHOW_SYM_LINKS |
                              base::FileEnumerator::FILES |
                              base::FileEnumerator::DIRECTORIES,
                          pattern);
  for (auto path = it.Next(); !path.empty(); path = it.Next()) {
    auto sub_res = GlobInternal(path, patterns, idx + 1);
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
  Value ret(Value::Type::DICTIONARY);

  for (const auto& key : keys) {
    string file_name = GetFileName(key);
    if (base::FilePath{file_name}.IsAbsolute()) {
      LOG(ERROR) << "file_name " << file_name << " is absolute";
      return base::nullopt;
    }
    string key_name = GetKeyName(key);
    const auto file_path = dir_path.Append(file_name);
    string content;

    // missing file
    if (!base::PathExists(file_path)) {
      LOG(ERROR) << file_path.value() << " doesn't exist";
      return base::nullopt;
    }

    // File exists, but somehow we can't read it.
    if (!ReadFileToStringWithMaxSize(file_path, &content, kReadFileMaxSize)) {
      LOG(ERROR) << file_path.value() << " exists, but we can't read it";
      return base::nullopt;
    }

    ret.SetStringKey(key_name, TrimWhitespaceASCII(content, TRIM_ALL));
  }

  for (const auto& key : optional_keys) {
    string file_name = GetFileName(key);
    if (base::FilePath{file_name}.IsAbsolute()) {
      LOG(ERROR) << "file_name " << file_name << " is absolute";
      return base::nullopt;
    }
    string key_name = GetKeyName(key);
    const auto file_path = dir_path.Append(file_name);
    string content;

    if (!base::PathExists(file_path))
      continue;

    if (ReadFileToStringWithMaxSize(file_path, &content, kReadFileMaxSize))
      ret.SetStringKey(key_name, TrimWhitespaceASCII(content, TRIM_ALL));
  }

  return ret;
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
  return GlobInternal(base::FilePath(components[0]), components, 1);
}

std::vector<base::FilePath> Glob(const std::string& pattern) {
  return Glob(base::FilePath(pattern));
}

}  // namespace runtime_probe
