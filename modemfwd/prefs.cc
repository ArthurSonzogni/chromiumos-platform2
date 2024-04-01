// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/prefs.h"

#include <memory>

#include <base/logging.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/file_utils.h>

using base::FilePath;
using std::string;

namespace modemfwd {

Prefs::Prefs(const base::FilePath& prefs_root) : prefs_root_(prefs_root) {}

// static
std::unique_ptr<Prefs> Prefs::CreatePrefs(base::FilePath root_path) {
  auto prefs_root = base::FilePath(root_path);
  if (!base::DirectoryExists(prefs_root)) {
    return nullptr;
  }
  return std::make_unique<Prefs>(prefs_root);
}

// static
std::unique_ptr<Prefs> Prefs::CreatePrefs(const Prefs& parent,
                                          const std::string& sub_pref) {
  if (!base::DirectoryExists(parent.GetPrefRootPath())) {
    return nullptr;
  }
  auto prefs_root = parent.GetPrefRootPath().Append(sub_pref);
  if (!base::DirectoryExists(prefs_root)) {
    base::File::Error file_err;
    if (!base::CreateDirectoryAndGetError(prefs_root, &file_err)) {
      PLOG(ERROR) << "Failed to create directory '" << prefs_root.value()
                  << "': " << base::File::ErrorToString(file_err);
      return nullptr;
    }
  }
  return std::make_unique<Prefs>(prefs_root);
}

base::FilePath Prefs::GetKeyPrefPath(const string& key) {
  return prefs_root_.Append(key);
}

bool Prefs::SetKey(const string& key, const string& value) {
  auto key_path = GetKeyPrefPath(key);
  if (!base::WriteFile(key_path, value)) {
    PLOG(ERROR) << "Failed to write to prefs: " << key_path.value();
    return false;
  }
  return true;
}

bool Prefs::KeyValueMatches(const std::string& key, const std::string& value) {
  std::string contents;
  if (!GetKey(key, &contents)) {
    return false;
  }
  return contents == value;
}

bool Prefs::GetKey(const string& key, string* value) {
  auto key_path = GetKeyPrefPath(key);
  if (!base::ReadFileToString(key_path, value)) {
    PLOG(ERROR) << "Failed to read from prefs: " << key_path.value();
    return false;
  }
  return true;
}

bool Prefs::Create(const string& key) {
  return SetKey(key, "");
}

bool Prefs::Exists(const string& key) {
  return base::PathExists(GetKeyPrefPath(key));
}

}  // namespace modemfwd
