// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/namespace_mounter/mounter.h"

#include <sys/mount.h>
#include <sys/stat.h>

#include <climits>
#include <memory>
#include <ostream>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback_helpers.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>
#include <libstorage/platform/platform.h>

#include "base/check.h"
#include "base/notreached.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount_constants.h"

namespace cryptohome {
namespace {

using base::FilePath;
using base::StringPrintf;
using brillo::cryptohome::home::GetRootPath;
using brillo::cryptohome::home::GetUserPath;
using brillo::cryptohome::home::SanitizeUserName;

const char kEphemeralCryptohomeRootContext[] =
    "u:object_r:cros_home_shadow_uid:s0";
const int kDefaultEcryptfsKeySize = kCryptohomeAesKeyBytes;

FilePath GetUserEphemeralMountDirectory(
    const ObfuscatedUsername& obfuscated_username) {
  return FilePath(kEphemeralCryptohomeDir)
      .Append(kEphemeralMountDir)
      .Append(*obfuscated_username);
}

FilePath GetMountedEphemeralRootHomePath(
    const ObfuscatedUsername& obfuscated_username) {
  return GetUserEphemeralMountDirectory(obfuscated_username)
      .Append(kRootHomeSuffix);
}

FilePath GetMountedEphemeralUserHomePath(
    const ObfuscatedUsername& obfuscated_username) {
  return GetUserEphemeralMountDirectory(obfuscated_username)
      .Append(kUserHomeSuffix);
}

// Sets up the SELinux context for a freshly mounted ephemeral cryptohome.
bool SetUpSELinuxContextForEphemeralCryptohome(libstorage::Platform* platform,
                                               const FilePath& source_path) {
  // Note that this is needed because the newly mounted ephemeral cryptohome is
  // a new file system, and thus the SELinux context that applies to the
  // mountpoint will not apply to the new root directory in the filesystem.
  return platform->SetSELinuxContext(source_path,
                                     kEphemeralCryptohomeRootContext);
}

constexpr mode_t kSkeletonSubDirMode = S_IRWXU | S_IRGRP | S_IXGRP;
constexpr mode_t kUserMountPointMode = S_IRWXU | S_IRGRP | S_IXGRP;
constexpr mode_t kRootMountPointMode = S_IRWXU;
constexpr mode_t kAccessMode = S_IRWXU | S_IRGRP | S_IXGRP;
constexpr mode_t kRootDirMode = S_IRWXU | S_IRWXG | S_ISVTX;

constexpr mode_t kTrackedDirMode = S_IRWXU;
constexpr mode_t kPathComponentDirMode = S_IRWXU;
constexpr mode_t kGroupWriteAccess = S_IWGRP;

struct DirectoryACL {
  FilePath path;
  mode_t mode;
  uid_t uid;
  gid_t gid;
};

std::vector<DirectoryACL> GetCacheSubdirectories(const FilePath& dir) {
  return std::vector<DirectoryACL>{
      {dir.Append(kUserHomeSuffix).Append(kGCacheDir), kAccessMode,
       libstorage::kChronosUid, libstorage::kChronosAccessGid},
      {dir.Append(kUserHomeSuffix).Append(kCacheDir), kTrackedDirMode,
       libstorage::kChronosUid, libstorage::kChronosGid},
      {dir.Append(kUserHomeSuffix)
           .Append(kGCacheDir)
           .Append(kGCacheVersion2Dir),
       kAccessMode | kGroupWriteAccess, libstorage::kChronosUid,
       libstorage::kChronosAccessGid},
      {dir.Append(kRootHomeSuffix).Append(kDaemonStoreCacheDir),
       kAccessMode | kGroupWriteAccess, libstorage::kRootUid,
       libstorage::kDaemonStoreGid},
  };
}

std::vector<DirectoryACL> GetCommonSubdirectories(const FilePath& dir,
                                                  bool bind_mount_downloads) {
  auto result = std::vector<DirectoryACL>{
      {dir.Append(kRootHomeSuffix), kRootDirMode, libstorage::kRootUid,
       libstorage::kDaemonStoreGid},
      {dir.Append(kUserHomeSuffix), kAccessMode, libstorage::kChronosUid,
       libstorage::kChronosAccessGid},
      {dir.Append(kUserHomeSuffix).Append(kMyFilesDir), kAccessMode,
       libstorage::kChronosUid, libstorage::kChronosAccessGid},
      {dir.Append(kUserHomeSuffix).Append(kMyFilesDir).Append(kDownloadsDir),
       kAccessMode, libstorage::kChronosUid, libstorage::kChronosAccessGid},
  };
  if (bind_mount_downloads) {
    result.push_back({dir.Append(kUserHomeSuffix).Append(kDownloadsDir),
                      kAccessMode, libstorage::kChronosUid,
                      libstorage::kChronosAccessGid});
  }
  auto cache_subdirs = GetCacheSubdirectories(dir);
  result.insert(result.end(), cache_subdirs.begin(), cache_subdirs.end());
  return result;
}

std::vector<DirectoryACL> GetDmcryptSubdirectories(const FilePath& dir,
                                                   bool bind_mount_downloads) {
  auto data_volume_subdirs =
      GetCommonSubdirectories(dir.Append(kMountDir), bind_mount_downloads);
  auto cache_volume_subdirs =
      GetCacheSubdirectories(dir.Append(kDmcryptCacheDir));

  auto result = cache_volume_subdirs;
  result.insert(result.end(), data_volume_subdirs.begin(),
                data_volume_subdirs.end());
  return result;
}

// Returns true if the directory should be root owned, but is missing or has
// wrong attributes.
bool IsRootDirectoryAndTampered(libstorage::Platform* platform,
                                DirectoryACL dir) {
  if (dir.uid != libstorage::kRootUid) {
    // Shouldn't be owned by root - ignore.
    return false;
  }

  base::stat_wrapper_t st;
  if (!platform->Stat(dir.path, &st)) {
    // Couldn't stat it, which means something is wrong, consider tampered.
    return true;
  }

  const mode_t st_mode = st.st_mode & 01777;
  if (S_ISDIR(st.st_mode) && st_mode == dir.mode && st.st_uid == dir.uid &&
      st.st_gid == dir.gid) {
    // Attributes are correct, not tampered
    return false;
  }

  LOG(ERROR) << "Root owned directory was tampered with, will be recreated.";
  return true;
}

void MaybeCorrectUserDirectoryAttrs(libstorage::Platform* platform,
                                    DirectoryACL dir) {
  // Ignore root owned directories - those are recreated if they have wrong
  // attributes.
  if (dir.uid == libstorage::kRootUid) {
    return;
  }
  // The check is intended to correct, report and fix a group mismatch for the
  // <vault> directories. It is initially required for crbug.com/1205308, but
  // since we are doing the chown anyway, there is no drama to do it for all
  // user directories.
  if (!platform->SafeDirChown(dir.path, dir.uid, dir.gid)) {
    LOG(ERROR) << "Failed to fix ownership of path directory" << dir.path;
  }

  // We make the mode for chronos-access accessible directories more
  // permissive, thus we need to change mode. It is unfortunate we need
  // to do it explicitly, unlike with mountpoints which we could just
  // recreate, but we must preserve user data while doing so.
  if (!platform->SafeDirChmod(dir.path, dir.mode)) {
    PLOG(ERROR) << "Failed to fix mode of path directory: " << dir.path;
  }
}

bool CreateVaultDirectoryStructure(
    libstorage::Platform* platform,
    const std::vector<DirectoryACL>& directories) {
  bool success = true;
  for (const auto& subdir : directories) {
    if (platform->DirectoryExists(subdir.path) &&
        !IsRootDirectoryAndTampered(platform, subdir)) {
      MaybeCorrectUserDirectoryAttrs(platform, subdir);
      continue;
    }

    if (!platform->DeletePathRecursively(subdir.path)) {
      LOG(ERROR) << "Couldn't cleanup path element: " << subdir.path;
      success = false;
      continue;
    }

    if (!platform->SafeCreateDirAndSetOwnershipAndPermissions(
            subdir.path, subdir.mode, subdir.uid, subdir.gid)) {
      LOG(ERROR) << "Couldn't create path directory: " << subdir.path;
      std::ignore = platform->DeletePathRecursively(subdir.path);
      success = false;
      continue;
    }
    LOG(INFO) << "Created vault subdirectory: " << subdir.path;
  }
  return success;
}

bool SetTrackingXattr(libstorage::Platform* platform,
                      const std::vector<DirectoryACL>& directories) {
  bool success = true;
  for (const auto& subdir : directories) {
    std::string name = subdir.path.BaseName().value();
    if (!platform->SetExtendedFileAttribute(subdir.path,
                                            kTrackedDirectoryNameAttribute,
                                            name.data(), name.length())) {
      PLOG(ERROR) << "Unable to set xattr on " << subdir.path;
      success = false;
      continue;
    }
  }
  return success;
}

// Identifies the pre-migration and post-migration stages of the ~/Downloads
// bind mount migration.
enum class MigrationStage { kUnknown, kMigrating, kMigrated };

// Converts MigrationStage to string.
std::string_view ToString(const MigrationStage stage) {
  switch (stage) {
    case MigrationStage::kUnknown:
      return "unknown";
    case MigrationStage::kMigrating:
      return kMigrating;
    case MigrationStage::kMigrated:
      return kMigrated;
  }

  NOTREACHED_NORETURN() << "Unexpected MigrationStage: "
                        << static_cast<int>(stage);
}

// Output operator for logging.
std::ostream& operator<<(std::ostream& out, const MigrationStage stage) {
  return out << ToString(stage);
}

MigrationStage GetDownloadsMigrationXattr(libstorage::Platform* const platform,
                                          const FilePath& path) {
  std::string xattr;
  DCHECK(platform);
  if (!platform->GetExtendedFileAttributeAsString(path, kMigrationXattrName,
                                                  &xattr)) {
    PLOG(ERROR) << "Cannot get xattr " << kMigrationXattrName << " of path '"
                << path << "'";
    return MigrationStage::kUnknown;
  }

  if (xattr == kMigrating) {
    return MigrationStage::kMigrating;
  }

  if (xattr == kMigrated) {
    return MigrationStage::kMigrated;
  }

  LOG(ERROR) << "Unexpected value '" << xattr << "' for xattr "
             << kMigrationXattrName << " of path '" << path << "'";
  return MigrationStage::kUnknown;
}

bool SetDownloadsMigrationXattr(libstorage::Platform* const platform,
                                const FilePath& path,
                                const MigrationStage stage) {
  DCHECK_NE(stage, MigrationStage::kUnknown);
  const auto xattr = ToString(stage);
  const bool ok = platform->SetExtendedFileAttribute(
      path, kMigrationXattrName, xattr.data(), xattr.size());
  PLOG_IF(ERROR, !ok) << "Cannot set xattr " << kMigrationXattrName << " on '"
                      << path << "' to '" << xattr << "'";
  return ok;
}

// Convert |mount_type| to a string for logging.
const char* MountTypeToString(MountType mount_type) {
  switch (mount_type) {
    case MountType::NONE:
      return "NONE";
    case MountType::ECRYPTFS:
      return "ECRYPTFS";
    case MountType::DIR_CRYPTO:
      return "DIR_CRYPTO";
    case MountType::DMCRYPT:
      return "DMCRYPT";
    case MountType::EPHEMERAL:
      return "EPHEMERAL";
    case MountType::ECRYPTFS_TO_DIR_CRYPTO:
      return "ECRYPTFS_TO_DIR_CRYPTO";
    case MountType::ECRYPTFS_TO_DMCRYPT:
      return "ECRYPTFS_TO_DMCRYPT";
    case MountType::DIR_CRYPTO_TO_DMCRYPT:
      return "DIR_CRYPTO_TO_DMCRYPT";
  }
}

class SyncGuard {
 public:
  explicit SyncGuard(libstorage::Platform* const platform)
      : platform_(platform) {
    DCHECK(platform_);
  }

  ~SyncGuard() {
    platform_->Sync();
    LOG(INFO) << "Sync'ed filesystems";
  }

 private:
  libstorage::Platform* const platform_;
};

// Truncates the given string `s` to the maximum length of `max_bytes`. Avoids
// cutting a multibyte UTF-8 sequence. Avoids cutting after a zero-width joiner.
std::string_view TruncateUtf8(std::string_view s, size_t max_bytes) {
  if (s.size() <= max_bytes) {
    // Nothing to truncate.
    return s;
  }

  // Remove the trailing bytes at the truncation point.
  while (max_bytes > 0 && (static_cast<std::uint8_t>(s[max_bytes]) &
                           0b1100'0000u) == 0b1000'0000u) {
    max_bytes--;
  }

  s = s.substr(0, max_bytes);

  // Remove the zero-width joiner if the truncated string would end with one.
  if (const std::string_view zwj = "\u200D"; s.ends_with(zwj)) {
    s.remove_suffix(zwj.size());
  }

  return s;
}

// Removes the numeric suffix at the end of the given string `s`. Does nothing
// if the string does not end with a numeric suffix. A numeric suffix is a
// decimal number between parentheses and preceded by a space, like:
// * " (1)" or
// * " (142857)".
void RemoveNumericSuffix(std::string& s) {
  size_t i = s.size();

  if (i == 0 || s[--i] != ')') {
    return;
  }

  if (i == 0 || !base::IsAsciiDigit(s[--i])) {
    return;
  }

  while (i > 0 && base::IsAsciiDigit(s[i - 1])) {
    --i;
  }

  if (i == 0 || s[--i] != '(') {
    return;
  }

  if (i == 0 || s[--i] != ' ') {
    return;
  }

  s.resize(i);
}

}  // namespace

const char kDefaultHomeDir[] = "/home/chronos/user";

// The extended attribute name used to designate the ~/Downloads folder pre and
// post migration.
constexpr char kMigrationXattrName[] = "user.BindMountMigration";

// Prior to moving ~/Downloads to ~/MyFiles/Downloads set the xattr above to
// this value.
constexpr char kMigrating[] = "migrating";

// After moving ~/Downloads to ~/MyFiles/Downloads set the xattr to this value.
constexpr char kMigrated[] = "migrated";

Mounter::Mounter(bool legacy_mount,
                 bool bind_mount_downloads,
                 libstorage::Platform* platform)
    : legacy_mount_(legacy_mount),
      bind_mount_downloads_(bind_mount_downloads),
      platform_(platform) {}

// static
FilePath Mounter::GetNewUserPath(const Username& username) {
  ObfuscatedUsername sanitized = SanitizeUserName(username);
  std::string user_dir = StringPrintf("u-%s", sanitized->c_str());
  return FilePath("/home").Append(kDefaultSharedUser).Append(user_dir);
}

FilePath Mounter::GetMountedUserHomePath(
    const ObfuscatedUsername& obfuscated_username) const {
  return GetUserMountDirectory(obfuscated_username).Append(kUserHomeSuffix);
}

FilePath Mounter::GetMountedRootHomePath(
    const ObfuscatedUsername& obfuscated_username) const {
  return GetUserMountDirectory(obfuscated_username).Append(kRootHomeSuffix);
}

bool Mounter::EnsurePathComponent(const FilePath& check_path,
                                  uid_t uid,
                                  gid_t gid) const {
  base::stat_wrapper_t st;
  if (!platform_->Stat(check_path, &st)) {
    // Dirent not there, so create and set ownership.
    if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
            check_path, kPathComponentDirMode, uid, gid)) {
      PLOG(ERROR) << "Can't create: " << check_path.value();
      return false;
    }
  } else {
    // Dirent there; make sure it's acceptable.
    if (!S_ISDIR(st.st_mode)) {
      LOG(ERROR) << "Non-directory path: " << check_path.value();
      return false;
    }
    if (st.st_uid != uid) {
      LOG(ERROR) << "Owner mismatch: " << check_path.value() << " " << st.st_uid
                 << " != " << uid;
      return false;
    }
    if (st.st_gid != gid) {
      LOG(ERROR) << "Group mismatch: " << check_path.value() << " " << st.st_gid
                 << " != " << gid;
      return false;
    }
    if (st.st_mode & S_IWOTH) {
      LOG(ERROR) << "Permissions too lenient: " << check_path.value() << " has "
                 << std::oct << st.st_mode;
      return false;
    }
  }
  return true;
}

bool Mounter::EnsureMountPointPath(const FilePath& dir) const {
  std::vector<std::string> path_parts = dir.GetComponents();
  FilePath check_path(path_parts[0]);
  if (path_parts[0] != "/") {
    return false;
  }
  for (size_t i = 1; i < path_parts.size(); i++) {
    check_path = check_path.Append(path_parts[i]);
    if (!EnsurePathComponent(check_path, libstorage::kRootUid,
                             libstorage::kRootGid)) {
      return false;
    }
  }
  return true;
}

bool Mounter::EnsureUserMountPoints(const Username& username) const {
  FilePath multi_home_user = GetUserPath(username);
  FilePath multi_home_root = GetRootPath(username);
  FilePath new_user_path = GetNewUserPath(username);

  if (platform_->DirectoryExists(multi_home_user) &&
      (platform_->IsDirectoryMounted(multi_home_user) ||
       !platform_->DeletePathRecursively(multi_home_user))) {
    PLOG(ERROR) << "Failed to remove mount point: " << multi_home_user.value();
    return false;
  }

  if (platform_->DirectoryExists(multi_home_root) &&
      (platform_->IsDirectoryMounted(multi_home_root) ||
       !platform_->DeletePathRecursively(multi_home_root))) {
    PLOG(ERROR) << "Failed to remove mount point: " << multi_home_root.value();
    return false;
  }

  if (platform_->DirectoryExists(new_user_path) &&
      (platform_->IsDirectoryMounted(new_user_path) ||
       !platform_->DeletePathRecursively(new_user_path))) {
    PLOG(ERROR) << "Failed to remove mount point: " << new_user_path.value();
    return false;
  }

  if (!EnsureMountPointPath(multi_home_user.DirName()) ||
      !EnsureMountPointPath(multi_home_root.DirName()) ||
      !EnsureMountPointPath(new_user_path.DirName().DirName()) ||
      !EnsurePathComponent(new_user_path.DirName(), libstorage::kChronosUid,
                           libstorage::kChronosGid)) {
    LOG(ERROR) << "The paths to mountpoints are inconsistent";
    return false;
  }

  if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
          multi_home_user, kUserMountPointMode, libstorage::kChronosUid,
          libstorage::kChronosAccessGid)) {
    PLOG(ERROR) << "Can't create: " << multi_home_user;
    return false;
  }

  if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
          new_user_path, kUserMountPointMode, libstorage::kChronosUid,
          libstorage::kChronosAccessGid)) {
    PLOG(ERROR) << "Can't create: " << new_user_path;
    return false;
  }

  if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
          multi_home_root, kRootMountPointMode, libstorage::kRootUid,
          libstorage::kRootGid)) {
    PLOG(ERROR) << "Can't create: " << multi_home_root;
    return false;
  }

  // TODO(b/300839936): Temporary verbose log.
  LOG(INFO) << "Finished ensuring user mount points";

  return true;
}

void Mounter::RecursiveCopy(const FilePath& source,
                            const FilePath& destination) const {
  std::unique_ptr<libstorage::FileEnumerator> file_enumerator(
      platform_->GetFileEnumerator(source, false, base::FileEnumerator::FILES));
  FilePath next_path;

  while (!(next_path = file_enumerator->Next()).empty()) {
    FilePath file_name = next_path.BaseName();

    FilePath destination_file = destination.Append(file_name);

    if (!platform_->Copy(next_path, destination_file) ||
        !platform_->SetOwnership(destination_file, libstorage::kChronosUid,
                                 libstorage::kChronosGid, false)) {
      LOG(ERROR) << "Couldn't change owner (" << libstorage::kChronosUid << ":"
                 << libstorage::kChronosGid
                 << ") of destination path: " << destination_file.value();
    }
  }

  std::unique_ptr<libstorage::FileEnumerator> dir_enumerator(
      platform_->GetFileEnumerator(source, false,
                                   base::FileEnumerator::DIRECTORIES));

  while (!(next_path = dir_enumerator->Next()).empty()) {
    FilePath dir_name = FilePath(next_path).BaseName();

    FilePath destination_dir = destination.Append(dir_name);
    VLOG(1) << "RecursiveCopy: " << destination_dir.value();

    if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
            destination_dir, kSkeletonSubDirMode, libstorage::kChronosUid,
            libstorage::kChronosGid)) {
      LOG(ERROR) << "SafeCreateDirAndSetOwnership() failed: "
                 << destination_dir.value();
    }

    RecursiveCopy(FilePath(next_path), destination_dir);
  }
}

void Mounter::CopySkeleton(const FilePath& destination) const {
  RecursiveCopy(SkelDir(), destination);
}

bool Mounter::IsFirstMountComplete(
    const ObfuscatedUsername& obfuscated_username) const {
  // TODO(b/300839936): Temporary verbose log.
  LOG(INFO) << "Checking if there has been a successful mount in the past";

  const FilePath mount_point = GetUserMountDirectory(obfuscated_username);
  const FilePath user_home = GetMountedUserHomePath(obfuscated_username);

  // Generate the set of the top level nodes that a mount creates.
  std::unordered_set<FilePath> initial_nodes;
  for (const auto& dir :
       GetCommonSubdirectories(mount_point, bind_mount_downloads_)) {
    initial_nodes.insert(dir.path);
  }
  std::unique_ptr<libstorage::FileEnumerator> skel_enumerator(
      platform_->GetFileEnumerator(
          SkelDir(), false,
          base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES));
  for (FilePath next = skel_enumerator->Next(); !next.empty();
       next = skel_enumerator->Next()) {
    initial_nodes.insert(user_home.Append(next.BaseName()));
  }

  // If we have any nodes within the vault that are not in the set created
  // above - it means we have successfully entered a user session prior.
  std::unique_ptr<libstorage::FileEnumerator> vault_enumerator(
      platform_->GetFileEnumerator(
          user_home, false,
          base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES));
  for (FilePath next = vault_enumerator->Next(); !next.empty();
       next = vault_enumerator->Next()) {
    if (initial_nodes.count(next) == 0) {
      // Found a file not from initial list, first mount was completed.
      // Log the file name to debug in case we ever see problems with
      // something racing the vault creation.
      LOG(INFO) << "Not a first mount, since found: " << next;
      return true;
    }
  }
  return false;
}

bool Mounter::MountLegacyHome(const FilePath& from) {
  VLOG(1) << "MountLegacyHome from " << from.value();
  // Multiple mounts can't live on the legacy mountpoint.
  if (platform_->IsDirectoryMounted(FilePath(kDefaultHomeDir))) {
    LOG(INFO) << "Skipping binding to /home/chronos/user";
    return true;
  }

  if (!BindAndPush(from, FilePath(kDefaultHomeDir),
                   libstorage::RemountOption::kMountsFlowIn))
    return false;

  return true;
}

bool Mounter::HandleMyFilesDownloads(const FilePath& user_home) {
  // If the flag to not bind mount ~/Downloads to ~/MyFiles/Downloads is
  // enabled, then attempt to (one-time) migrate the folder. In the event this
  // fails, fallback to the bind mount logic and try again on the next mount.
  if (!bind_mount_downloads_ && MoveDownloadsToMyFiles(user_home)) {
    return true;
  }

  const FilePath downloads = user_home.Append(kDownloadsDir);
  const FilePath downloads_in_my_files =
      user_home.Append(kMyFilesDir).Append(kDownloadsDir);

  // See b/172341309. User could have saved files in ~/MyFiles/Downloads in case
  // cryptohome crashed and bind-mounts were removed by error. Move the files
  // from ~/MyFiles/Downloads to ~/Downloads. In case the ~/Downloads folder had
  // been moved to ~/MyFiles/Downloads previously, this also acts as a "reverse"
  // migration.
  MoveDirectoryContents(downloads_in_my_files, downloads);

  // We also need to remove the xattr if it exists. This will allow the next
  // future migration of ~/Downloads to ~/MyFiles/Downloads to succeed again,
  // when the time comes.
  if (platform_->RemoveExtendedFileAttribute(downloads_in_my_files,
                                             kMigrationXattrName)) {
    LOG(INFO) << "Removed xattr '" << kMigrationXattrName << "' from '"
              << downloads_in_my_files << "'";
  }

  if (!BindAndPush(downloads, downloads_in_my_files)) {
    return false;
  }

  return true;
}

bool Mounter::MoveDownloadsToMyFiles(const FilePath& user_home) {
  using enum DownloadsMigrationStatus;
  const FilePath downloads_in_my_files =
      user_home.Append(kMyFilesDir).Append(kDownloadsDir);
  const FilePath downloads = user_home.Append(kDownloadsDir);

  // Check if the migration has successfully completed on a prior run.
  const MigrationStage stage =
      GetDownloadsMigrationXattr(platform_, downloads_in_my_files);

  bool ok;

  if (stage == MigrationStage::kMigrated) {
    LOG(INFO) << "The 'Downloads' folder is already marked as 'migrated'";
    ReportDownloadsMigrationStatus(kAlreadyMigrated);

    // Clean up the ~/Downloads folder if it reappeared after the migration.
    if (platform_->DirectoryExists(downloads)) {
      LOG(WARNING) << "The ~/Downloads folder reappeared after it was migrated "
                      "to ~/MyFiles/Downloads";
      ReportDownloadsMigrationStatus(kReappeared);

      MoveDirectoryContents(downloads, downloads_in_my_files);
      ok = platform_->DeleteFile(downloads);
      ReportDownloadsMigrationOperation("RemoveReappearedDownloads", ok);
      PLOG_IF(ERROR, !ok) << "Cannot remove the reappeared ~/Downloads folder";
      LOG_IF(INFO, ok) << "Removed the reappeared ~/Downloads folder";
    }

    // Clean up the old ~/Downloads-backup folder if it is still there.
    if (const FilePath downloads_backup = user_home.Append(kDownloadsBackupDir);
        platform_->DirectoryExists(downloads_backup)) {
      MoveDirectoryContents(downloads_backup, downloads_in_my_files);
      ok = platform_->DeleteFile(downloads_backup);
      ReportDownloadsMigrationOperation("CleanUp", ok);
      PLOG_IF(ERROR, !ok) << "Cannot delete the old ~/Downloads-backup folder";
      LOG_IF(INFO, ok) << "Deleted the old ~/Downloads-backup folder";
    }

    return true;
  }

  // Ensure that the filesystems will be sync'ed.
  const SyncGuard sync_guard(platform_);

  // If ~/Downloads doesn't exist and ~/MyFiles/Downloads does exist, this might
  // be a freshly set-up cryptohome or the previous xattr setting failed. Update
  // the xattr accordingly and, even if this fails, cryptohome is still in a
  // usable state so return true.
  if (!platform_->FileExists(downloads) &&
      platform_->FileExists(downloads_in_my_files)) {
    LOG(INFO) << "The 'Downloads' folder is already in ~/MyFiles/Downloads, "
                 "but its xattr is still marked as '"
              << stage << "'";

    if (stage == MigrationStage::kMigrating) {
      ok = SetDownloadsMigrationXattr(platform_, downloads_in_my_files,
                                      MigrationStage::kMigrated);
      ReportDownloadsMigrationOperation("FixXattr", ok);
      ReportDownloadsMigrationStatus(kFixXattr);
    } else {
      DCHECK_EQ(stage, MigrationStage::kUnknown);
      LOG(INFO) << "It looks like a new cryptohome";
      ok = SetDownloadsMigrationXattr(platform_, downloads_in_my_files,
                                      MigrationStage::kMigrated);
      ReportDownloadsMigrationOperation("SetXattrForNewCryptoHome", ok);
      ReportDownloadsMigrationStatus(kSetXattrForNewCryptoHome);
    }

    if (!ok) {
      ReportDownloadsMigrationStatus(kCannotSetXattrToMigrated);
    }

    return true;
  }

  // Move all files from ~/MyFiles/Downloads to ~/Downloads to ensure there's
  // none left in ~/MyFiles/Downloads before migration.
  MoveDirectoryContents(downloads_in_my_files, downloads);

  // Set the xattr for the ~/Downloads directory to be "migrating". If this
  // fails, don't continue as the filesystem is in a good state to continue with
  // the bind-mount and a migration can be done at a later stage.
  ok = SetDownloadsMigrationXattr(platform_, downloads,
                                  MigrationStage::kMigrating);
  ReportDownloadsMigrationOperation("SetXattrToMigrating", ok);
  if (!ok) {
    ReportDownloadsMigrationStatus(kCannotSetXattrToMigrating);
    return false;
  }

  // Exchange ~/Downloads to ~/MyFiles/Downloads.
  ok = platform_->Exchange(downloads, downloads_in_my_files);
  ReportDownloadsMigrationOperation("Exchange", ok);
  if (!ok) {
    PLOG(ERROR) << "Cannot exchange ~/Downloads and ~/MyFiles/Downloads";
    ReportDownloadsMigrationStatus(kCannotMoveToMyFiles);
    return false;
  }

  LOG(INFO) << "Moved ~/Downloads into ~/MyFiles";

  // Remove the old Downloads folder.
  ok = platform_->DeleteFile(downloads);
  ReportDownloadsMigrationOperation("CleanUp", ok);
  PLOG_IF(ERROR, !ok) << "Cannot delete old ~/Downloads folder";
  LOG_IF(INFO, ok) << "Deleted old ~/Downloads folder";

  // The migration has completed successfully, to ensure no further migrations
  // occur, set the xattr to "migrated". If this fails, the cryptohome is usable
  // and, the next time this migration logic runs, it will try and update the
  // xattr again.
  ok = SetDownloadsMigrationXattr(platform_, downloads_in_my_files,
                                  MigrationStage::kMigrated);
  ReportDownloadsMigrationOperation("SetXattrToMigrated", ok);
  if (!ok) {
    ReportDownloadsMigrationStatus(kCannotSetXattrToMigrated);
    return true;
  }

  // This is considered the point of no return. The migration has, for all
  // intents and purposes, successfully completed.
  ReportDownloadsMigrationStatus(kSuccess);
  LOG(INFO) << "The ~/Downloads folder was successfully migrated to "
               "~/MyFiles/Downloads and marked as 'migrated'";
  return true;
}

bool Mounter::MountAndPush(const FilePath& src,
                           const FilePath& dest,
                           const std::string& type,
                           const std::string& options) {
  uint32_t mount_flags = libstorage::kDefaultMountFlags | MS_NOSYMFOLLOW;

  if (!platform_->Mount(src, dest, type, mount_flags, options)) {
    PLOG(ERROR) << "Mount failed: " << src.value() << " -> " << dest.value();
    return false;
  }

  stack_.Push(src, dest);
  return true;
}

bool Mounter::BindAndPush(const FilePath& src,
                          const FilePath& dest,
                          libstorage::RemountOption remount) {
  if (!platform_->Bind(src, dest, remount, /*nosymfollow=*/true)) {
    std::string remount_strs[] = {"kNoRemount", "kPrivate", "kShared",
                                  "kMountsFlowIn", "kUnbindable"};
    PLOG(ERROR) << "Bind mount failed: " << src.value() << " -> "
                << dest.value()
                << " remount: " << remount_strs[static_cast<int>(remount)];
    return false;
  }

  stack_.Push(src, dest);
  return true;
}

bool Mounter::MountDaemonStoreCacheDirectories(
    const FilePath& root_home, const ObfuscatedUsername& obfuscated_username) {
  return InternalMountDaemonStoreDirectories(
      root_home.Append(kDaemonStoreCacheDir), obfuscated_username,
      kEtcDaemonStoreBaseDir, kRunDaemonStoreCacheBaseDir);
}

bool Mounter::MountDaemonStoreDirectories(
    const FilePath& root_home, const ObfuscatedUsername& obfuscated_username) {
  return InternalMountDaemonStoreDirectories(root_home, obfuscated_username,
                                             kEtcDaemonStoreBaseDir,
                                             kRunDaemonStoreBaseDir);
}

bool Mounter::InternalMountDaemonStoreDirectories(
    const FilePath& root_home,
    const ObfuscatedUsername& obfuscated_username,
    const char* etc_daemon_store_base_dir,
    const char* run_daemon_store_base_dir) {
  // Iterate over all directories in |etc_daemon_store_base_dir|. This list is
  // on rootfs, so it's tamper-proof and nobody can sneak in additional
  // directories that we blindly mount. The actual mounts happen on
  // |run_daemon_store_base_dir|, though.
  std::unique_ptr<libstorage::FileEnumerator> file_enumerator(
      platform_->GetFileEnumerator(FilePath(etc_daemon_store_base_dir),
                                   false /* recursive */,
                                   base::FileEnumerator::DIRECTORIES));

  // |etc_daemon_store_base_dir|/<daemon-name>
  FilePath etc_daemon_store_path;
  while (!(etc_daemon_store_path = file_enumerator->Next()).empty()) {
    const FilePath& daemon_name = etc_daemon_store_path.BaseName();

    // |run_daemon_store_base_dir|/<daemon-name>
    FilePath run_daemon_store_path =
        FilePath(run_daemon_store_base_dir).Append(daemon_name);
    if (!platform_->DirectoryExists(run_daemon_store_path)) {
      // The chromeos_startup script should make sure this exist.
      PLOG(ERROR) << "Daemon store directory does not exist: "
                  << run_daemon_store_path.value();
      return false;
    }

    // Typically, one of:
    //   /home/.shadow/<user_hash>/mount/root/<daemon-name>
    //   /home/.shadow/<user_hash>/mount/root/.cache/<daemon-name>
    const FilePath mount_source = root_home.Append(daemon_name);

    // |run_daemon_store_base_dir|/<daemon-name>/<user_hash>
    const FilePath mount_target =
        run_daemon_store_path.Append(*obfuscated_username);

    // Copy ownership from |etc_daemon_store_path| to |mount_source|. After the
    // bind operation, this guarantees that ownership for |mount_target| is the
    // same as for |etc_daemon_store_path| (usually
    // <daemon_user>:<daemon_group>), which is what the daemon intended.
    // Otherwise, it would end up being root-owned.
    base::stat_wrapper_t etc_daemon_path_stat =
        file_enumerator->GetInfo().stat();

    // TODO(dlunev): add some reporting when we see ACL mismatch.
    if (platform_->DirectoryExists(mount_source)) {
      if (!platform_->SafeDirChmod(mount_source,
                                   etc_daemon_path_stat.st_mode)) {
        LOG(ERROR) << "Failed to chmod directory " << mount_source.value();
        return false;
      }
    } else {
      if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
              mount_source, etc_daemon_path_stat.st_mode,
              etc_daemon_path_stat.st_uid, etc_daemon_path_stat.st_gid)) {
        LOG(ERROR) << "Failed to create directory " << mount_source.value();
        return false;
      }
    }

    // The target directory's parent exists in the root mount namespace so the
    // directory itself can be created in the root mount namespace and it will
    // be visible in all namespaces.
    if (!platform_->CreateDirectory(mount_target)) {
      PLOG(ERROR) << "Failed to create directory " << mount_target.value();
      return false;
    }

    // Assuming that |run_daemon_store_path| is a shared mount and the daemon
    // runs in a file system namespace with |run_daemon_store_path| mounted as
    // secondary, this mount event propagates into the daemon.
    if (!BindAndPush(mount_source, mount_target))
      return false;
  }

  return true;
}

bool Mounter::MoveWithConflictResolution(const FilePath& from,
                                         const FilePath& to_dir,
                                         ProbeCounts& probe_counts) const {
  DCHECK(from.IsAbsolute()) << from;
  DCHECK(to_dir.IsAbsolute()) << to_dir;

  std::string name = from.BaseName().value();
  DCHECK(!name.empty());
  DCHECK(!name.starts_with('/')) << " for " << name;

  // Try to move the item without renaming it.
  {
    const FilePath to = to_dir.Append(name);
    if (platform_->RenameNoReplace(from, to)) {
      // Successfully moved the item.
      return true;
    }

    // Item cannot be moved. Check the reason.
    if (errno != EEXIST) {
      PLOG(ERROR) << "Cannot move '" << from << "' to '" << to << "'";
      return false;
    }
  }

  // There was a name collision in the destination directory. Get the filename
  // extension if the source item is a file (and not a directory).
  std::string ext;
  if (base::stat_wrapper_t st; !name.ends_with('.') &&
                               platform_->Stat(from, &st) &&
                               S_ISREG(st.st_mode)) {
    ext = FilePath(name).Extension();
    // See b/333986056. Work around some of the limitations of FilePath.
    if (ext.size() == name.size() || ext.size() > 12 ||
        ext.find(' ') != std::string::npos) {
      ext = FilePath(name).FinalExtension();
      if (ext.size() == name.size() || ext.size() > 6 ||
          ext.find(' ') != std::string::npos) {
        ext.clear();
      }
    }

    if (!ext.empty()) {
      name.resize(name.size() - ext.size());
      DCHECK(!name.empty());
    }
  }

  RemoveNumericSuffix(name);

  for (int& i = probe_counts[base::StrCat({name, ext})]; ++i < INT_MAX;) {
    const std::string suffix =
        base::StrCat({" (", base::NumberToString(i), ")", ext});

    // Try to move and rename the item at the same time.
    const FilePath to = to_dir.Append(
        base::StrCat({TruncateUtf8(name, NAME_MAX - suffix.size()), suffix}));
    if (platform_->RenameNoReplace(from, to)) {
      // Successfully moved and renamed the item.
      return true;
    }

    // Item cannot be moved. Check the reason.
    if (errno != EEXIST) {
      PLOG(ERROR) << "Cannot move '" << from << "' to '" << to << "'";
      return false;
    }
  }

  LOG(ERROR) << "Cannot move '" << from << "' to dir '" << to_dir
             << "': Too many collisions";
  return false;
}

void Mounter::MoveDirectoryContents(const FilePath& from_dir,
                                    const FilePath& to_dir) const {
  VLOG(1) << "Migrating items from '" << from_dir << "' to '" << to_dir << "'";

  const std::unique_ptr<libstorage::FileEnumerator> enumerator(
      platform_->GetFileEnumerator(
          from_dir, false /* recursive */,
          base::FileEnumerator::DIRECTORIES | base::FileEnumerator::FILES));

  DCHECK(enumerator);
  int num_items = 0;
  int num_moved = 0;
  ProbeCounts probe_counts;

  for (FilePath from = enumerator->Next(); !from.empty();
       from = enumerator->Next()) {
    num_items++;
    const bool ok = MoveWithConflictResolution(from, to_dir, probe_counts);
    ReportDownloadsMigrationOperation("UnmaskItem", ok);
    num_moved += ok;
  }

  LOG_IF(INFO, num_moved != 0) << "Moved " << num_moved << " items from '"
                               << from_dir << "' to '" << to_dir << "'";

  ReportMaskedDownloadsItems(num_items);
}

bool Mounter::MountHomesAndDaemonStores(
    const Username& username,
    const ObfuscatedUsername& obfuscated_username,
    const FilePath& user_home,
    const FilePath& root_home) {
  // Bind mount user directory as a shared bind mount.
  // This allows us to set up user mounts as subsidiary mounts without needing
  // to replicate that across multiple mount points.
  if (!BindAndPush(user_home, user_home, libstorage::RemountOption::kShared))
    return false;

  // Same as above for |root_home|, to ensure submounts are propagated
  // correctly.
  if (!BindAndPush(root_home, root_home, libstorage::RemountOption::kShared))
    return false;

  // Mount /home/chronos/user.
  if (legacy_mount_ && !MountLegacyHome(user_home))
    return false;

  // Mount /home/chronos/u-<user_hash>
  const FilePath new_user_path = GetNewUserPath(username);
  if (!BindAndPush(user_home, new_user_path,
                   libstorage::RemountOption::kMountsFlowIn))
    return false;

  // Mount /home/user/<user_hash>.
  const FilePath user_multi_home = GetUserPath(username);
  if (!BindAndPush(user_home, user_multi_home,
                   libstorage::RemountOption::kMountsFlowIn))
    return false;

  // Mount /home/root/<user_hash>.
  const FilePath root_multi_home = GetRootPath(username);
  if (!BindAndPush(root_home, root_multi_home,
                   libstorage::RemountOption::kMountsFlowIn))
    return false;

  // Mount Downloads to MyFiles/Downloads in the user shadow directory.
  if (!HandleMyFilesDownloads(user_home)) {
    return false;
  }

  // Mount directories used by daemons to store per-user data.
  if (!MountDaemonStoreDirectories(root_home, obfuscated_username))
    return false;

  return true;
}

bool Mounter::MountCacheSubdirectories(
    const ObfuscatedUsername& obfuscated_username,
    const FilePath& data_directory) {
  FilePath cache_directory = GetDmcryptUserCacheDirectory(obfuscated_username);

  const FilePath tracked_subdir_paths[] = {
      FilePath(kUserHomeSuffix).Append(kCacheDir),
      FilePath(kUserHomeSuffix).Append(kGCacheDir),
      FilePath(kRootHomeSuffix).Append(kDaemonStoreCacheDir),
  };

  for (const auto& tracked_dir : tracked_subdir_paths) {
    FilePath src_dir = cache_directory.Append(tracked_dir);
    FilePath dst_dir = data_directory.Append(tracked_dir);

    if (!BindAndPush(src_dir, dst_dir,
                     libstorage::RemountOption::kMountsFlowIn)) {
      LOG(ERROR) << "Failed to bind mount " << src_dir;
      return false;
    }
  }

  return true;
}

// The eCryptfs mount is mounted from vault/ --> mount/ except in case of
// migration where the mount point is a temporary directory.
bool Mounter::SetUpEcryptfsMount(const ObfuscatedUsername& obfuscated_username,
                                 const std::string& fek_signature,
                                 const std::string& fnek_signature,
                                 const FilePath& mount_point) {
  const FilePath vault_path = GetEcryptfsUserVaultPath(obfuscated_username);

  // Specify the ecryptfs options for mounting the user's cryptohome.
  std::string ecryptfs_options = StringPrintf(
      "ecryptfs_cipher=aes"
      ",ecryptfs_key_bytes=%d"
      ",ecryptfs_fnek_sig=%s"
      ",ecryptfs_sig=%s"
      ",ecryptfs_unlink_sigs",
      kDefaultEcryptfsKeySize, fnek_signature.c_str(), fek_signature.c_str());

  // Create <vault_path>/user and <vault_path>/root.
  std::ignore = CreateVaultDirectoryStructure(
      platform_, GetCommonSubdirectories(vault_path, bind_mount_downloads_));

  // b/115997660: Mount eCryptfs after creating the tracked subdirectories.
  if (!MountAndPush(vault_path, mount_point, "ecryptfs", ecryptfs_options)) {
    LOG(ERROR) << "eCryptfs mount failed";
    return false;
  }

  return true;
}

void Mounter::SetUpDircryptoMount(
    const ObfuscatedUsername& obfuscated_username) {
  // TODO(b/300839936): Temporary verbose logging.
  LOG(INFO) << "Setting up dircrypto mount";

  const FilePath mount_point = GetUserMountDirectory(obfuscated_username);

  LOG(INFO) << "Creating vault directory structure";
  std::ignore = CreateVaultDirectoryStructure(
      platform_, GetCommonSubdirectories(mount_point, bind_mount_downloads_));
  LOG(INFO) << "Setting tracking xattr";
  std::ignore = SetTrackingXattr(
      platform_, GetCommonSubdirectories(mount_point, bind_mount_downloads_));
  LOG(INFO) << "Finished setting up dircrypto mount";
}

bool Mounter::SetUpDmcryptMount(const ObfuscatedUsername& obfuscated_username,
                                const FilePath& data_mount_point) {
  const FilePath dmcrypt_data_volume =
      GetDmcryptDataVolume(obfuscated_username);
  const FilePath dmcrypt_cache_volume =
      GetDmcryptCacheVolume(obfuscated_username);

  const FilePath cache_mount_point =
      GetDmcryptUserCacheDirectory(obfuscated_username);

  // Mount the data volume at <vault>/mount and the cache volume at
  // <vault>/cache. The directories are set up by the creation code.
  if (!MountAndPush(dmcrypt_data_volume, data_mount_point,
                    kDmcryptContainerMountType,
                    kDmcryptContainerMountOptions)) {
    LOG(ERROR) << "Failed to mount dmcrypt data volume";
    return false;
  }

  if (!MountAndPush(dmcrypt_cache_volume, cache_mount_point,
                    kDmcryptContainerMountType,
                    kDmcryptContainerMountOptions)) {
    LOG(ERROR) << "Failed to mount dmcrypt cache volume";
    return false;
  }

  std::ignore = CreateVaultDirectoryStructure(
      platform_, GetDmcryptSubdirectories(UserPath(obfuscated_username),
                                          bind_mount_downloads_));

  return true;
}

StorageStatus Mounter::PerformMount(MountType mount_type,
                                    const Username& username,
                                    const std::string& fek_signature,
                                    const std::string& fnek_signature) {
  LOG(INFO) << "Performing mount of type " << MountTypeToString(mount_type);

  const ObfuscatedUsername obfuscated_username = SanitizeUserName(username);

  if (!EnsureUserMountPoints(username)) {
    return StorageStatus::Make(FROM_HERE, "Error creating mountpoints",
                               MOUNT_ERROR_CREATE_CRYPTOHOME_FAILED);
  }

  // Since Service::Mount cleans up stale mounts, we should only reach
  // this point if someone attempts to re-mount an in-use mount point.
  if (platform_->IsDirectoryMounted(
          GetUserMountDirectory(obfuscated_username))) {
    return StorageStatus::Make(
        FROM_HERE,
        std::string("Mount point is busy: ") +
            GetUserMountDirectory(obfuscated_username).value(),
        MOUNT_ERROR_FATAL);
  }

  const FilePath user_home = GetMountedUserHomePath(obfuscated_username);
  const FilePath root_home = GetMountedRootHomePath(obfuscated_username);

  switch (mount_type) {
    case MountType::ECRYPTFS:
      if (!SetUpEcryptfsMount(obfuscated_username, fek_signature,
                              fnek_signature,
                              GetUserMountDirectory(obfuscated_username))) {
        return StorageStatus::Make(FROM_HERE, "Can't setup ecryptfs",
                                   MOUNT_ERROR_MOUNT_ECRYPTFS_FAILED);
      }
      break;
    case MountType::ECRYPTFS_TO_DIR_CRYPTO:
      if (!SetUpEcryptfsMount(
              obfuscated_username, fek_signature, fnek_signature,
              GetUserTemporaryMountDirectory(obfuscated_username))) {
        return StorageStatus::Make(
            FROM_HERE, "Can't setup ecryptfs for migration to fscrypt",
            MOUNT_ERROR_MOUNT_ECRYPTFS_FAILED);
      }
      SetUpDircryptoMount(obfuscated_username);
      return StorageStatus::Ok();
    case MountType::ECRYPTFS_TO_DMCRYPT:
      if (!SetUpEcryptfsMount(
              obfuscated_username, fek_signature, fnek_signature,
              GetUserTemporaryMountDirectory(obfuscated_username))) {
        return StorageStatus::Make(
            FROM_HERE, "Can't setup ecryptfs for migration to dmcrypt",
            MOUNT_ERROR_MOUNT_ECRYPTFS_FAILED);
      }
      if (!SetUpDmcryptMount(obfuscated_username,
                             GetUserMountDirectory(obfuscated_username))) {
        return StorageStatus::Make(
            FROM_HERE, "Can't setup dmcrypt to migrate from ecryptfs",
            MOUNT_ERROR_MOUNT_DMCRYPT_FAILED);
      }

      if (!MountCacheSubdirectories(
              obfuscated_username,
              GetUserMountDirectory(obfuscated_username))) {
        return StorageStatus::Make(
            FROM_HERE, "Can't setup dmcrypt cache to migrate from ecryptfs",
            MOUNT_ERROR_MOUNT_DMCRYPT_FAILED);
      }
      if (!MountDaemonStoreCacheDirectories(root_home, obfuscated_username)) {
        return StorageStatus::Make(FROM_HERE, "Can't mount daemon-store-cache",
                                   MOUNT_ERROR_MOUNT_DMCRYPT_FAILED);
      }
      return StorageStatus::Ok();
    case MountType::DIR_CRYPTO:
      SetUpDircryptoMount(obfuscated_username);
      break;
    case MountType::DIR_CRYPTO_TO_DMCRYPT:
      SetUpDircryptoMount(obfuscated_username);
      if (!SetUpDmcryptMount(
              obfuscated_username,
              GetUserTemporaryMountDirectory(obfuscated_username))) {
        return StorageStatus::Make(
            FROM_HERE, "Can't setup dmcrypt to migrate from fscrypt",
            MOUNT_ERROR_MOUNT_DMCRYPT_FAILED);
      }

      if (!MountCacheSubdirectories(
              obfuscated_username,
              GetUserTemporaryMountDirectory(obfuscated_username))) {
        return StorageStatus::Make(
            FROM_HERE, "Can't setup dmcrypt cache to migrate from fscrypt",
            MOUNT_ERROR_MOUNT_DMCRYPT_FAILED);
      }
      if (!MountDaemonStoreCacheDirectories(root_home, obfuscated_username)) {
        return StorageStatus::Make(FROM_HERE, "Can't mount daemon-store-cache",
                                   MOUNT_ERROR_MOUNT_DMCRYPT_FAILED);
      }
      return StorageStatus::Ok();
    case MountType::DMCRYPT:
      if (!SetUpDmcryptMount(obfuscated_username,
                             GetUserMountDirectory(obfuscated_username))) {
        return StorageStatus::Make(FROM_HERE, "Dm-crypt mount failed",
                                   MOUNT_ERROR_MOUNT_DMCRYPT_FAILED);
      }
      break;
    case MountType::EPHEMERAL:
    case MountType::NONE:
      NOTREACHED();
  }

  if (!IsFirstMountComplete(obfuscated_username)) {
    CopySkeleton(user_home);
  }

  // When migrating, it's better to avoid exposing the new ext4 crypto dir.
  if (!MountHomesAndDaemonStores(username, obfuscated_username, user_home,
                                 root_home)) {
    return StorageStatus::Make(
        FROM_HERE, "Can't mount home or daemonstore",
        MOUNT_ERROR_MOUNT_HOMES_AND_DAEMON_STORES_FAILED);
  }

  // TODO(sarthakkukreti): This can't be moved due to child mount propagation
  // issues. Figure out how to make it propagate properly to move to the switch
  // above.
  if (mount_type == MountType::DMCRYPT &&
      !MountCacheSubdirectories(obfuscated_username,
                                GetUserMountDirectory(obfuscated_username))) {
    return StorageStatus::Make(
        FROM_HERE,
        "Failed to mount tracked subdirectories from the cache volume",
        MOUNT_ERROR_MOUNT_DMCRYPT_FAILED);
  }

  // Mount daemon store cache directories from .cache into  /run/daemon-store.
  if (!MountDaemonStoreCacheDirectories(root_home, obfuscated_username)) {
    return StorageStatus::Make(FROM_HERE, "Can't mount daemon-store-cache",
                               MOUNT_ERROR_MOUNT_DMCRYPT_FAILED);
  }

  return StorageStatus::Ok();
}

// TODO(dlunev): make specific errors returned. MOUNT_ERROR_FATAL for now
// to preserve the existing expectations..
StorageStatus Mounter::PerformEphemeralMount(
    const Username& username, const FilePath& ephemeral_loop_device) {
  const ObfuscatedUsername obfuscated_username = SanitizeUserName(username);
  const FilePath mount_point =
      GetUserEphemeralMountDirectory(obfuscated_username);
  LOG(ERROR) << "Directory is" << mount_point.value();

  if (!platform_->CreateDirectory(mount_point)) {
    return StorageStatus::Make(
        FROM_HERE, "Directory creation failed for " + mount_point.value(),
        MOUNT_ERROR_FATAL);
  }
  if (!MountAndPush(ephemeral_loop_device, mount_point, kEphemeralMountType,
                    kEphemeralMountOptions)) {
    return StorageStatus::Make(FROM_HERE, "Can't mount ephemeral",
                               MOUNT_ERROR_FATAL);
  }

  // Set SELinux context first, so that the created user & root directory have
  // the correct context.
  if (!SetUpSELinuxContextForEphemeralCryptohome(platform_, mount_point)) {
    return StorageStatus::Make(FROM_HERE,
                               "Can't setup SELinux context for ephemeral",
                               MOUNT_ERROR_FATAL);
  }

  if (!EnsureUserMountPoints(username)) {
    return StorageStatus::Make(
        FROM_HERE, "Can't ensure mountpoints for ephemeral", MOUNT_ERROR_FATAL);
  }

  const FilePath user_home =
      GetMountedEphemeralUserHomePath(obfuscated_username);

  const FilePath root_home =
      GetMountedEphemeralRootHomePath(obfuscated_username);

  if (!CreateVaultDirectoryStructure(
          platform_,
          GetCommonSubdirectories(mount_point, bind_mount_downloads_))) {
    return StorageStatus::Make(FROM_HERE,
                               "Can't create vault structure for ephemeral",
                               MOUNT_ERROR_FATAL);
  }

  CopySkeleton(user_home);

  if (!MountHomesAndDaemonStores(username, obfuscated_username, user_home,
                                 root_home)) {
    return StorageStatus::Make(FROM_HERE,
                               "Can't mount home and daemonstore for ephemeral",
                               MOUNT_ERROR_FATAL);
  }

  if (!MountDaemonStoreCacheDirectories(root_home, obfuscated_username)) {
    return StorageStatus::Make(
        FROM_HERE, "Can't mount home and daemon-store-cache for ephemeral",
        MOUNT_ERROR_FATAL);
  }

  return StorageStatus::Ok();
}

void Mounter::UnmountAll() {
  FilePath src, dest;
  while (stack_.Pop(&src, &dest)) {
    ForceUnmount(src, dest);
  }

  // Clean up destination directory for ephemeral loop device mounts.
  const FilePath ephemeral_mount_path =
      FilePath(kEphemeralCryptohomeDir).Append(kEphemeralMountDir);
  platform_->DeletePathRecursively(ephemeral_mount_path);
}

void Mounter::ForceUnmount(const FilePath& src, const FilePath& dest) {
  // Try an immediate unmount.
  bool was_busy;
  if (!platform_->Unmount(dest, false, &was_busy)) {
    LOG(ERROR) << "Couldn't unmount '" << dest.value()
               << "' immediately, was_busy=" << std::boolalpha << was_busy;
    // Failed to unmount immediately, do a lazy unmount.  If |was_busy| we also
    // want to sync before the unmount to help prevent data loss.
    if (was_busy)
      platform_->SyncDirectory(dest);
    platform_->LazyUnmount(dest);
    platform_->SyncDirectory(src);
  }
}

bool Mounter::CanPerformEphemeralMount() const {
  return !MountPerformed();
}

bool Mounter::MountPerformed() const {
  return stack_.size() > 0;
}

bool Mounter::IsPathMounted(const FilePath& path) const {
  return stack_.ContainsDest(path);
}

std::vector<FilePath> Mounter::MountedPaths() const {
  return stack_.MountDestinations();
}

}  // namespace cryptohome
