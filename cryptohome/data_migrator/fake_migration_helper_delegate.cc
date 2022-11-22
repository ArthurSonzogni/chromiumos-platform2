// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/data_migrator/fake_migration_helper_delegate.h"

#include <base/containers/contains.h>

namespace cryptohome::data_migrator {

FakeMigrationHelperDelegate::FakeMigrationHelperDelegate() = default;

FakeMigrationHelperDelegate::~FakeMigrationHelperDelegate() = default;

void FakeMigrationHelperDelegate::AddDenylistedPath(
    const base::FilePath& path) {
  denylisted_paths_.insert(path);
}

void FakeMigrationHelperDelegate::ClearDenylistedPaths() {
  denylisted_paths_.clear();
}

bool FakeMigrationHelperDelegate::ShouldMigrateFile(
    const base::FilePath& child) {
  return !base::Contains(denylisted_paths_, child);
}

bool FakeMigrationHelperDelegate::ShouldCopyQuotaProjectId() {
  return true;
}

}  // namespace cryptohome::data_migrator
