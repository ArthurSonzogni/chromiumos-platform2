// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/data_migrator/fake_migration_helper_delegate.h"

#include <string>

#include <base/containers/contains.h>

namespace cryptohome::data_migrator {

namespace {

constexpr char kMtimeXattrName[] = "user.mtime";
constexpr char kAtimeXattrName[] = "user.atime";

}  // namespace

FakeMigrationHelperDelegate::FakeMigrationHelperDelegate() = default;

FakeMigrationHelperDelegate::~FakeMigrationHelperDelegate() = default;

void FakeMigrationHelperDelegate::AddDenylistedPath(
    const base::FilePath& path) {
  denylisted_paths_.insert(path);
}

void FakeMigrationHelperDelegate::AddXattrMapping(const std::string& name_from,
                                                  const std::string& name_to) {
  xattr_mappings_[name_from] = name_to;
}

bool FakeMigrationHelperDelegate::ShouldMigrateFile(
    const base::FilePath& child) {
  return !base::Contains(denylisted_paths_, child);
}

bool FakeMigrationHelperDelegate::ShouldCopyQuotaProjectId() {
  return true;
}

std::string FakeMigrationHelperDelegate::GetMtimeXattrName() {
  return kMtimeXattrName;
}

std::string FakeMigrationHelperDelegate::GetAtimeXattrName() {
  return kAtimeXattrName;
}

std::string FakeMigrationHelperDelegate::ConvertXattrName(
    const std::string& name) {
  auto iter = xattr_mappings_.find(name);
  if (iter != xattr_mappings_.end()) {
    return iter->second;
  }
  return name;
}

}  // namespace cryptohome::data_migrator
