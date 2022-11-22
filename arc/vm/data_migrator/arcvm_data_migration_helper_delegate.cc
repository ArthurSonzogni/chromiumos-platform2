// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/vm/data_migrator/arcvm_data_migration_helper_delegate.h"

#include <sys/stat.h>

#include <array>
#include <optional>
#include <string>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

namespace arc::data_migrator {

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

// Struct to describe a single range of Android UID/GID mapping.
// 'T' is either uid_t or gid_t.
template <typename T>
struct IdMap {
  T guest;  // start of range on the guest side.
  T host;   // start of range on the host side.
  T size;   // size of the range of the mapping.
};

// UID mappings for Android's /data directory done by virtio-fs.
// Taken from platform2/vm_tools/concierge/vm_util.cc (originally from
// platform2/arc/container/bundle/pi/config.json).
constexpr std::array<IdMap<uid_t>, 3> kAndroidUidMap{{
    {0, 655360, 5000},
    {5000, 600, 50},
    {5050, 660410, 1994950},
}};

// GID equivalent of |kAndroidUidMap|.
constexpr std::array<IdMap<gid_t>, 5> kAndroidGidMap{{
    {0, 655360, 1065},
    {1065, 20119, 1},
    {1066, 656426, 3934},
    {5000, 600, 50},
    {5050, 660410, 1994950},
}};

template <typename T, size_t N>
std::optional<T> MapToGuestId(T host_id,
                              const std::array<IdMap<T>, N>& id_maps,
                              base::StringPiece id_name) {
  for (const auto& id_map : id_maps) {
    if (id_map.host <= host_id && host_id < id_map.host + id_map.size) {
      return host_id - id_map.host + id_map.guest;
    }
  }
  LOG(ERROR) << "Failed to translate host " << id_name << ": " << host_id;
  return std::nullopt;
}

}  // namespace

ArcVmDataMigrationHelperDelegate::ArcVmDataMigrationHelperDelegate(
    ArcVmDataMigratorMetrics* metrics)
    : metrics_(metrics) {}

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

bool ArcVmDataMigrationHelperDelegate::ConvertFileMetadata(
    base::stat_wrapper_t* stat) {
  std::optional<uid_t> guest_uid =
      MapToGuestId(stat->st_uid, kAndroidUidMap, "UID");
  std::optional<gid_t> guest_gid =
      MapToGuestId(stat->st_gid, kAndroidGidMap, "GID");
  if (!guest_uid.has_value() || !guest_gid.has_value()) {
    return false;
  }
  stat->st_uid = guest_uid.value();
  stat->st_gid = guest_gid.value();
  return true;
}

std::string ArcVmDataMigrationHelperDelegate::ConvertXattrName(
    const std::string& name) {
  if (base::StartsWith(name, kVirtiofsSecurityXattrPrefix)) {
    return name.substr(std::char_traits<char>::length(kVirtiofsXattrPrefix));
  }
  return name;
}

void ArcVmDataMigrationHelperDelegate::ReportStartTime() {
  migration_start_time_ = base::TimeTicks::Now();
}

void ArcVmDataMigrationHelperDelegate::ReportEndTime() {
  metrics_->ReportDuration(base::TimeTicks::Now() - migration_start_time_);
}

void ArcVmDataMigrationHelperDelegate::ReportStartStatus(
    cryptohome::data_migrator::MigrationStartStatus status) {
  metrics_->ReportStartStatus(status);
}

void ArcVmDataMigrationHelperDelegate::ReportEndStatus(
    cryptohome::data_migrator::MigrationEndStatus status) {
  metrics_->ReportEndStatus(status);
}

void ArcVmDataMigrationHelperDelegate::ReportTotalSize(int total_byte_count_mb,
                                                       int total_file_count) {
  metrics_->ReportTotalByteCountInMb(total_byte_count_mb);
  metrics_->ReportTotalFileCount(total_file_count);
}

void ArcVmDataMigrationHelperDelegate::ReportFailure(
    base::File::Error error_code,
    cryptohome::data_migrator::MigrationFailedOperationType type,
    const base::FilePath& path,
    cryptohome::data_migrator::FailureLocationType location_type) {
  metrics_->ReportFailedErrorCode(error_code);
  metrics_->ReportFailedOperationType(type);
  // TODO(b/272151802): Report failed path type too.
}

void ArcVmDataMigrationHelperDelegate::ReportFailedNoSpace(
    int initial_free_space_mb, int failure_free_space_mb) {
  metrics_->ReportInitialFreeSpace(initial_free_space_mb);
  metrics_->ReportNoSpaceFailureFreeSpace(failure_free_space_mb);
}

void ArcVmDataMigrationHelperDelegate::ReportFailedNoSpaceXattrSizeInBytes(
    int total_xattr_size_bytes) {
  metrics_->ReportNoSpaceXattrSize(total_xattr_size_bytes);
}

}  // namespace arc::data_migrator
