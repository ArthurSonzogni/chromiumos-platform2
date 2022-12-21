// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/data_migrator/arcvm_data_migration_helper_delegate.h"

#include <string>

namespace arc {

namespace {

constexpr char kMtimeXattrName[] = "trusted.ArcVmDataMigrationMtime";
constexpr char kAtimeXattrName[] = "trusted.ArcVmDataMigrationAtime";

}  // namespace

ArcVmDataMigrationHelperDelegate::ArcVmDataMigrationHelperDelegate() = default;

ArcVmDataMigrationHelperDelegate::~ArcVmDataMigrationHelperDelegate() = default;

bool ArcVmDataMigrationHelperDelegate::ShouldCopyQuotaProjectId() {
  return true;
}

std::string ArcVmDataMigrationHelperDelegate::GetMtimeXattrName() {
  return kMtimeXattrName;
}

std::string ArcVmDataMigrationHelperDelegate::GetAtimeXattrName() {
  return kAtimeXattrName;
}

}  // namespace arc
