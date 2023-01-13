// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/data_migrator/arcvm_data_migration_helper_delegate.h"

#include <string>

#include <base/strings/string_util.h>

namespace arc {

namespace {

constexpr char kMtimeXattrName[] = "trusted.ArcVmDataMigrationMtime";
constexpr char kAtimeXattrName[] = "trusted.ArcVmDataMigrationAtime";

// Virtio-fs translates security.* xattrs in ARCVM to user.virtiofs.security.*
// on the host-side (b/155443663), so convert them back to security.* xattr in
// the migration.
constexpr char kVirtiofsSecurityXattrPrefix[] = "user.virtiofs.security.";
constexpr char kVirtiofsXattrPrefix[] = "user.virtiofs.";

static_assert(base::StringPiece(kVirtiofsSecurityXattrPrefix)
                  .find(kVirtiofsXattrPrefix) == 0);

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

std::string ArcVmDataMigrationHelperDelegate::ConvertXattrName(
    const std::string& name) {
  if (base::StartsWith(name, kVirtiofsSecurityXattrPrefix)) {
    return name.substr(std::char_traits<char>::length(kVirtiofsXattrPrefix));
  }
  return name;
}

}  // namespace arc
