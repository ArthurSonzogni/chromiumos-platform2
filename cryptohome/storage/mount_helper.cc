// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/storage/mount_helper.h"

#include <sys/stat.h>

#include <memory>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <brillo/cryptohome.h>
#include <brillo/secure_blob.h>

#include "cryptohome/cryptohome_common.h"
#include "cryptohome/cryptohome_metrics.h"
#include "cryptohome/filesystem_layout.h"
#include "cryptohome/storage/homedirs.h"
#include "cryptohome/storage/mount_constants.h"

using base::FilePath;
using base::StringPrintf;
using brillo::cryptohome::home::GetRootPath;
using brillo::cryptohome::home::GetUserPath;
using brillo::cryptohome::home::SanitizeUserName;
using brillo::cryptohome::home::SanitizeUserNameWithSalt;

namespace cryptohome {
const char kEphemeralCryptohomeRootContext[] =
    "u:object_r:cros_home_shadow_uid:s0";
}

namespace {
constexpr uid_t kMountOwnerUid = 0;
constexpr gid_t kMountOwnerGid = 0;
constexpr gid_t kDaemonStoreGid = 400;

const int kDefaultEcryptfsKeySize = CRYPTOHOME_AES_KEY_BYTES;

FilePath GetUserEphemeralMountDirectory(
    const std::string& obfuscated_username) {
  return FilePath(cryptohome::kEphemeralCryptohomeDir)
      .Append(cryptohome::kEphemeralMountDir)
      .Append(obfuscated_username);
}

FilePath GetMountedEphemeralRootHomePath(
    const std::string& obfuscated_username) {
  return GetUserEphemeralMountDirectory(obfuscated_username)
      .Append(cryptohome::kRootHomeSuffix);
}

FilePath GetMountedEphemeralUserHomePath(
    const std::string& obfuscated_username) {
  return GetUserEphemeralMountDirectory(obfuscated_username)
      .Append(cryptohome::kUserHomeSuffix);
}

FilePath VaultPathToUserPath(const FilePath& vault) {
  return vault.Append(cryptohome::kUserHomeSuffix);
}

FilePath VaultPathToRootPath(const FilePath& vault) {
  return vault.Append(cryptohome::kRootHomeSuffix);
}

// Sets up the SELinux context for a freshly mounted ephemeral cryptohome.
bool SetUpSELinuxContextForEphemeralCryptohome(cryptohome::Platform* platform,
                                               const FilePath& source_path) {
  // Note that this is needed because the newly mounted ephemeral cryptohome is
  // a new file system, and thus the SELinux context that applies to the
  // mountpoint will not apply to the new root directory in the filesystem.
  return platform->SetSELinuxContext(
      source_path, cryptohome::kEphemeralCryptohomeRootContext);
}

constexpr mode_t kSkeletonSubDirMode = S_IRWXU | S_IRGRP | S_IXGRP;
constexpr mode_t kUserMountPointMode = S_IRWXU | S_IRGRP | S_IXGRP;
constexpr mode_t kRootMountPointMode = S_IRWXU;
constexpr mode_t kAccessMode = S_IRWXU | S_IRGRP | S_IXGRP;
constexpr mode_t kRootDirMode = S_IRWXU | S_IRWXG | S_ISVTX;

constexpr mode_t kTrackedDirMode = S_IRWXU;
constexpr mode_t kPathComponentDirMode = S_IRWXU;
constexpr mode_t kGroupWriteAccess = S_IWGRP;

}  // namespace

namespace cryptohome {

const char kDefaultHomeDir[] = "/home/chronos/user";

std::vector<DirectoryACL> MountHelper::GetCommonSubdirectories(
    uid_t uid, gid_t gid, gid_t access_gid) {
  return std::vector<DirectoryACL>{
      {FilePath(kUserHomeSuffix).Append(kDownloadsDir), kAccessMode, uid,
       access_gid},
      {FilePath(kUserHomeSuffix).Append(kMyFilesDir), kAccessMode, uid,
       access_gid},
      {FilePath(kUserHomeSuffix).Append(kMyFilesDir).Append(kDownloadsDir),
       kAccessMode, uid, access_gid},
  };
}

std::vector<DirectoryACL> MountHelper::GetCacheSubdirectories(
    uid_t uid, gid_t gid, gid_t access_gid) {
  return std::vector<DirectoryACL>{
      {FilePath(kUserHomeSuffix).Append(kGCacheDir), kAccessMode, uid,
       access_gid},
      {FilePath(kUserHomeSuffix).Append(kCacheDir), kTrackedDirMode, uid, gid},
  };
}

std::vector<DirectoryACL> MountHelper::GetGCacheSubdirectories(uid_t uid,
                                                               gid_t gid,
                                                               gid_t access_gid,
                                                               bool v1_dirs) {
  DirectoryACL gcache_v2_subdir = {
      FilePath(kUserHomeSuffix).Append(kGCacheDir).Append(kGCacheVersion2Dir),
      kAccessMode | kGroupWriteAccess, uid, access_gid};

  std::vector<DirectoryACL> gcache_v1_subdirs = {
      {FilePath(kUserHomeSuffix).Append(kGCacheDir).Append(kGCacheVersion1Dir),
       kAccessMode, uid, access_gid},
      {FilePath(kUserHomeSuffix)
           .Append(kGCacheDir)
           .Append(kGCacheVersion1Dir)
           .Append(kGCacheBlobsDir),
       kTrackedDirMode, uid, gid},
      {FilePath(kUserHomeSuffix)
           .Append(kGCacheDir)
           .Append(kGCacheVersion1Dir)
           .Append(kGCacheTmpDir),
       kTrackedDirMode, uid, gid},
  };

  if (v1_dirs) {
    gcache_v1_subdirs.push_back(gcache_v2_subdir);
    return gcache_v1_subdirs;
  }

  return {gcache_v2_subdir};
}

std::vector<DirectoryACL> MountHelper::GetTrackedSubdirectories(
    uid_t uid, gid_t gid, gid_t access_gid) {
  std::vector<DirectoryACL> durable_only_subdirs{
      {FilePath(kRootHomeSuffix), kRootDirMode, kMountOwnerUid,
       kDaemonStoreGid},
      {FilePath(kUserHomeSuffix), kAccessMode, uid, access_gid},
  };

  std::vector<DirectoryACL> common_subdirs =
      GetCommonSubdirectories(uid, gid, access_gid);

  std::vector<DirectoryACL> cache_subdirs =
      GetCacheSubdirectories(uid, gid, access_gid);

  std::vector<DirectoryACL> gcache_subdirs =
      GetGCacheSubdirectories(uid, gid, access_gid, /*v1_dirs=*/true);

  auto result = durable_only_subdirs;
  result.insert(result.end(), common_subdirs.begin(), common_subdirs.end());
  result.insert(result.end(), cache_subdirs.begin(), cache_subdirs.end());
  result.insert(result.end(), gcache_subdirs.begin(), gcache_subdirs.end());
  return result;
}

// static
FilePath MountHelper::GetNewUserPath(const std::string& username) {
  std::string sanitized = SanitizeUserName(username);
  std::string user_dir = StringPrintf("u-%s", sanitized.c_str());
  return FilePath("/home")
      .Append(cryptohome::kDefaultSharedUser)
      .Append(user_dir);
}

// static
FilePath MountHelper::GetEphemeralSparseFile(
    const std::string& obfuscated_username) {
  return FilePath(cryptohome::kEphemeralCryptohomeDir)
      .Append(kSparseFileDir)
      .Append(obfuscated_username);
}

FilePath MountHelper::GetMountedUserHomePath(
    const std::string& obfuscated_username) const {
  return GetUserMountDirectory(obfuscated_username).Append(kUserHomeSuffix);
}

FilePath MountHelper::GetMountedRootHomePath(
    const std::string& obfuscated_username) const {
  return GetUserMountDirectory(obfuscated_username).Append(kRootHomeSuffix);
}

bool MountHelper::EnsurePathComponent(const FilePath& check_path,
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

void MountHelper::CreateHomeSubdirectories(const FilePath& vault_path) const {
  FilePath user_path(VaultPathToUserPath(vault_path));
  FilePath root_path(VaultPathToRootPath(vault_path));
  base::stat_wrapper_t st;

  // This check makes the creation idempotent; if we completed creation,
  // root_path will exist and we're done, and if we didn't complete it, we can
  // finish it.
  if (platform_->Stat(root_path, &st) && S_ISDIR(st.st_mode) &&
      st.st_mode & S_ISVTX && st.st_uid == kMountOwnerUid &&
      st.st_gid == kDaemonStoreGid) {
    // This reports whether the existing user directory has the correct group.
    // TODO(crbug.com/1205308): Remove once the root cause is fixed and we stop
    // seeing cases where this directory has the wrong group owner.
    if (platform_->Stat(user_path, &st)) {
      bool correct = st.st_gid == default_access_gid_;
      ReportUserSubdirHasCorrectGroup(correct);
      if (!correct) {
        LOG(ERROR) << "Group mismatch in user directory: " << user_path.value()
                   << " " << st.st_gid << " != " << default_access_gid_;
        if (!platform_->SafeDirChown(user_path, default_uid_,
                                     default_access_gid_)) {
          LOG(ERROR) << "Failed to fix ownership of user directory";
        }
      }
    }
    return;
  }

  // There are three ways to get here:
  // 1) the Stat() call above succeeded, but what we saw was not a root-owned
  //    directory.
  // 2) the Stat() call above failed with -ENOENT
  // 3) the Stat() call above failed for some other reason
  // In any of these cases, it is safe for us to rm root_path, since the only
  // way it could have gotten there is if someone undertook some funny business
  // as root.
  platform_->DeletePathRecursively(root_path);

  if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
          user_path, kAccessMode, default_uid_, default_access_gid_)) {
    PLOG(ERROR) << "SafeCreateDirAndSetOwnershipAndPermissions() failed: "
                << user_path.value();
    return;
  }

  // Create root_path at the end as a sentinel for migration.
  if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
          root_path, kRootDirMode, kMountOwnerUid, kDaemonStoreGid)) {
    PLOG(ERROR) << "SafeCreateDirAndSetOwnershipAndPermissions() failed: "
                << root_path.value();
    return;
  }
  LOG(INFO) << "Created user directory: " << vault_path.value();
}

bool MountHelper::EnsureMountPointPath(const FilePath& dir) const {
  std::vector<std::string> path_parts;
  dir.GetComponents(&path_parts);
  FilePath check_path(path_parts[0]);
  if (path_parts[0] != "/") {
    return false;
  }
  for (size_t i = 1; i < path_parts.size(); i++) {
    check_path = check_path.Append(path_parts[i]);
    if (!EnsurePathComponent(check_path, kMountOwnerUid, kMountOwnerGid)) {
      return false;
    }
  }
  return true;
}

bool MountHelper::EnsureUserMountPoints(const std::string& username) const {
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
      !EnsurePathComponent(new_user_path.DirName(), default_uid_,
                           default_gid_)) {
    LOG(ERROR) << "The paths to mountpoints are inconsistent";
    return false;
  }

  if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
          multi_home_user, kUserMountPointMode, default_uid_,
          default_access_gid_)) {
    PLOG(ERROR) << "Can't create: " << multi_home_user;
    return false;
  }

  if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
          new_user_path, kUserMountPointMode, default_uid_,
          default_access_gid_)) {
    PLOG(ERROR) << "Can't create: " << new_user_path;
    return false;
  }

  if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
          multi_home_root, kRootMountPointMode, kMountOwnerUid,
          kMountOwnerGid)) {
    PLOG(ERROR) << "Can't create: " << multi_home_root;
    return false;
  }

  return true;
}

void MountHelper::RecursiveCopy(const FilePath& source,
                                const FilePath& destination) const {
  std::unique_ptr<cryptohome::FileEnumerator> file_enumerator(
      platform_->GetFileEnumerator(source, false, base::FileEnumerator::FILES));
  FilePath next_path;
  while (!(next_path = file_enumerator->Next()).empty()) {
    FilePath file_name = next_path.BaseName();
    FilePath destination_file = destination.Append(file_name);
    if (!platform_->Copy(next_path, destination_file) ||
        !platform_->SetOwnership(destination_file, default_uid_, default_gid_,
                                 false)) {
      LOG(ERROR) << "Couldn't change owner (" << default_uid_ << ":"
                 << default_gid_
                 << ") of destination path: " << destination_file.value();
    }
  }
  std::unique_ptr<cryptohome::FileEnumerator> dir_enumerator(
      platform_->GetFileEnumerator(source, false,
                                   base::FileEnumerator::DIRECTORIES));
  while (!(next_path = dir_enumerator->Next()).empty()) {
    FilePath dir_name = FilePath(next_path).BaseName();
    FilePath destination_dir = destination.Append(dir_name);
    VLOG(1) << "RecursiveCopy: " << destination_dir.value();
    if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
            destination_dir, kSkeletonSubDirMode, default_uid_, default_gid_)) {
      LOG(ERROR) << "SafeCreateDirAndSetOwnership() failed: "
                 << destination_dir.value();
    }
    RecursiveCopy(FilePath(next_path), destination_dir);
  }
}

void MountHelper::CopySkeleton(const FilePath& destination) const {
  RecursiveCopy(SkelDir(), destination);
}

std::vector<DirectoryACL> MountHelper::GetEphemeralSubdirectories(
    uid_t uid, gid_t gid, gid_t access_gid) {
  std::vector<DirectoryACL> common_subdirs =
      GetCommonSubdirectories(uid, gid, access_gid);

  std::vector<DirectoryACL> cache_subdirs =
      GetCacheSubdirectories(uid, gid, access_gid);

  std::vector<DirectoryACL> gcache_subdirs =
      GetGCacheSubdirectories(uid, gid, access_gid, /*v1_dirs=*/false);

  auto result = common_subdirs;
  result.insert(result.end(), cache_subdirs.begin(), cache_subdirs.end());
  result.insert(result.end(), gcache_subdirs.begin(), gcache_subdirs.end());
  return result;
}

bool MountHelper::SetUpEphemeralCryptohome(const FilePath& source_path) {
  FilePath user_home = source_path.Append(kUserHomeSuffix);
  CopySkeleton(user_home);

  const auto subdirs = GetEphemeralSubdirectories(default_uid_, default_gid_,
                                                  default_access_gid_);
  for (const auto& subdir : subdirs) {
    FilePath path = FilePath(source_path).Append(subdir.path);
    if (platform_->DirectoryExists(path))
      continue;

    if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
            path, subdir.mode, subdir.uid, subdir.gid)) {
      LOG(ERROR) << "Couldn't create user path directory: " << path.value();
      return false;
    }
  }

  return true;
}

bool MountHelper::MountLegacyHome(const FilePath& from) {
  VLOG(1) << "MountLegacyHome from " << from.value();
  // Multiple mounts can't live on the legacy mountpoint.
  if (platform_->IsDirectoryMounted(FilePath(kDefaultHomeDir))) {
    LOG(INFO) << "Skipping binding to /home/chronos/user";
    return true;
  }

  if (!BindAndPush(from, FilePath(kDefaultHomeDir),
                   RemountOption::kMountsFlowIn))
    return false;

  return true;
}

bool MountHelper::BindMyFilesDownloads(const base::FilePath& user_home) {
  const FilePath downloads = user_home.Append(kDownloadsDir);
  const FilePath downloads_in_myfiles =
      user_home.Append(kMyFilesDir).Append(kDownloadsDir);

  // User could have saved files in MyFiles/Downloads in case cryptohome
  // crashed and bind mounts were removed by error. See crbug.com/1080730.
  // Move the files back to Download unless a file already exists.
  MigrateDirectory(downloads, downloads_in_myfiles);

  if (!BindAndPush(downloads, downloads_in_myfiles))
    return false;

  return true;
}

bool MountHelper::MountAndPush(const base::FilePath& src,
                               const base::FilePath& dest,
                               const std::string& type,
                               const std::string& options) {
  if (!platform_->Mount(src, dest, type, kDefaultMountFlags, options)) {
    PLOG(ERROR) << "Mount failed: " << src.value() << " -> " << dest.value();
    return false;
  }

  stack_.Push(src, dest);
  return true;
}

bool MountHelper::BindAndPush(const FilePath& src,
                              const FilePath& dest,
                              RemountOption remount) {
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

bool MountHelper::MountDaemonStoreDirectories(
    const FilePath& root_home, const std::string& obfuscated_username) {
  // Iterate over all directories in /etc/daemon-store. This list is on rootfs,
  // so it's tamper-proof and nobody can sneak in additional directories that we
  // blindly mount. The actual mounts happen on /run/daemon-store, though.
  std::unique_ptr<cryptohome::FileEnumerator> file_enumerator(
      platform_->GetFileEnumerator(FilePath(kEtcDaemonStoreBaseDir),
                                   false /* recursive */,
                                   base::FileEnumerator::DIRECTORIES));

  // /etc/daemon-store/<daemon-name>
  FilePath etc_daemon_store_path;
  while (!(etc_daemon_store_path = file_enumerator->Next()).empty()) {
    const FilePath& daemon_name = etc_daemon_store_path.BaseName();

    // /run/daemon-store/<daemon-name>
    FilePath run_daemon_store_path =
        FilePath(kRunDaemonStoreBaseDir).Append(daemon_name);
    if (!platform_->DirectoryExists(run_daemon_store_path)) {
      // The chromeos_startup script should make sure this exist.
      PLOG(ERROR) << "Daemon store directory does not exist: "
                  << run_daemon_store_path.value();
      return false;
    }

    // /home/.shadow/<user_hash>/mount/root/<daemon-name>
    const FilePath mount_source = root_home.Append(daemon_name);

    // /run/daemon-store/<daemon-name>/<user_hash>
    const FilePath mount_target =
        run_daemon_store_path.Append(obfuscated_username);

    // Copy ownership from |etc_daemon_store_path| to |mount_source|. After the
    // bind operation, this guarantees that ownership for |mount_target| is the
    // same as for |etc_daemon_store_path| (usually
    // <daemon_user>:<daemon_group>), which is what the daemon intended.
    // Otherwise, it would end up being root-owned.
    base::stat_wrapper_t etc_daemon_path_stat =
        file_enumerator->GetInfo().stat();

    // TODO(dlunev): add some reporting when we see ACL mismatch.
    if (!platform_->DirectoryExists(mount_source) &&
        !platform_->SafeCreateDirAndSetOwnershipAndPermissions(
            mount_source, etc_daemon_path_stat.st_mode,
            etc_daemon_path_stat.st_uid, etc_daemon_path_stat.st_gid)) {
      LOG(ERROR) << "Failed to create directory " << mount_source.value();
      return false;
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

void MountHelper::MigrateDirectory(const base::FilePath& dst,
                                   const base::FilePath& src) const {
  VLOG(1) << "Migrating directory " << src << " -> " << dst;
  std::unique_ptr<cryptohome::FileEnumerator> enumerator(
      platform_->GetFileEnumerator(
          src, false /* recursive */,
          base::FileEnumerator::DIRECTORIES | base::FileEnumerator::FILES));
  for (base::FilePath src_obj = enumerator->Next(); !src_obj.empty();
       src_obj = enumerator->Next()) {
    base::FilePath dst_obj = dst.Append(src_obj.BaseName());

    // If the destination file exists, or rename failed for whatever reason,
    // then log a warning and delete the source file.
    if (platform_->FileExists(dst_obj) ||
        !platform_->Rename(src_obj, dst_obj)) {
      LOG(WARNING) << "Failed to migrate " << src_obj << " : deleting";
      platform_->DeletePathRecursively(src_obj);
    }
  }
}

bool MountHelper::MountHomesAndDaemonStores(
    const std::string& username,
    const std::string& obfuscated_username,
    const FilePath& user_home,
    const FilePath& root_home) {
  // Bind mount user directory as a shared bind mount.
  // This allows us to set up user mounts as subsidiary mounts without needing
  // to replicate that across multiple mount points.
  if (!BindAndPush(user_home, user_home, RemountOption::kShared))
    return false;

  // Mount /home/chronos/user.
  if (legacy_mount_ && !MountLegacyHome(user_home))
    return false;

  // Mount /home/chronos/u-<user_hash>
  const FilePath new_user_path = GetNewUserPath(username);
  if (!BindAndPush(user_home, new_user_path, RemountOption::kMountsFlowIn))
    return false;

  // Mount /home/user/<user_hash>.
  const FilePath user_multi_home = GetUserPath(username);
  if (!BindAndPush(user_home, user_multi_home, RemountOption::kMountsFlowIn))
    return false;

  // Mount /home/root/<user_hash>.
  const FilePath root_multi_home = GetRootPath(username);
  if (!BindAndPush(root_home, root_multi_home, RemountOption::kMountsFlowIn))
    return false;

  if (bind_mount_downloads_) {
    // Mount Downloads to MyFiles/Downloads in the user shadow directory.
    if (!BindMyFilesDownloads(user_home)) {
      return false;
    }
  }

  // Mount directories used by daemons to store per-user data.
  if (!MountDaemonStoreDirectories(root_home, obfuscated_username))
    return false;

  return true;
}

bool MountHelper::CreateTrackedSubdirectories(
    const std::string& obfuscated_username, const MountType& mount_type) const {
  // Add the subdirectories if they do not exist.
  const FilePath dest_dir(mount_type == MountType::ECRYPTFS
                              ? GetEcryptfsUserVaultPath(obfuscated_username)
                              : GetUserMountDirectory(obfuscated_username));
  if (!platform_->DirectoryExists(dest_dir)) {
    LOG(ERROR) << "Can't create tracked subdirectories for a missing user.";
    return false;
  }

  const FilePath mount_dir(GetUserMountDirectory(obfuscated_username));

  // The call is allowed to partially fail if directory creation fails, but we
  // want to have as many of the specified tracked directories created as
  // possible.
  bool result = true;
  for (const auto& tracked_dir : GetTrackedSubdirectories(
           default_uid_, default_gid_, default_access_gid_)) {
    const FilePath tracked_dir_path = dest_dir.Append(tracked_dir.path);
    if (mount_type == MountType::ECRYPTFS) {
      const FilePath userside_dir = mount_dir.Append(tracked_dir.path);
      // If non-pass-through dir with the same name existed - delete it
      // to prevent duplication.
      if (platform_->DirectoryExists(userside_dir) &&
          !platform_->DirectoryExists(tracked_dir_path)) {
        platform_->DeletePathRecursively(userside_dir);
      }
    }

    // Create pass-through directory.
    if (!platform_->DirectoryExists(tracked_dir_path)) {
      // Delete the existing file or symbolic link if any.
      platform_->DeleteFile(tracked_dir_path);
      VLOG(1) << "Creating pass-through directory " << tracked_dir_path.value();
      if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
              tracked_dir_path, tracked_dir.mode, tracked_dir.uid,
              tracked_dir.gid)) {
        PLOG(ERROR) << "Couldn't create directory: "
                    << tracked_dir_path.value();
        platform_->DeletePathRecursively(tracked_dir_path);
        result = false;
        continue;
      }
    } else {
      // We make the mode for chronos-access accessible directories more
      // permissive, thus we need to change mode. it is unfortunate we need
      // to do it explicitly, unlike with mountpoints which we could just
      // recreate, but we must preservce user data while doing so.
      if (!platform_->SafeDirChmod(tracked_dir_path, tracked_dir.mode)) {
        PLOG(ERROR) << "Couldn't change directory's mode: "
                    << tracked_dir_path.value();
      }
    }
    if (mount_type == MountType::DIR_CRYPTO) {
      // Set xattr to make this directory trackable.
      std::string name = tracked_dir_path.BaseName().value();
      if (!platform_->SetExtendedFileAttribute(tracked_dir_path,
                                               kTrackedDirectoryNameAttribute,
                                               name.data(), name.length())) {
        PLOG(ERROR) << "Unable to set xattr on " << tracked_dir_path.value();
        result = false;
        continue;
      }
    }
  }

  if (!bind_mount_downloads_) {
    // If we are not doing the downloads bind mount, move the content of the
    // Downloads to MyFiles/Downloads. Doing it file by file in case there is
    // a content in the MyFiles/Downloads already.
    auto downloads = dest_dir.Append(kUserHomeSuffix).Append(kDownloadsDir);
    auto downloads_in_myfiles = dest_dir.Append(kUserHomeSuffix)
                                    .Append(kMyFilesDir)
                                    .Append(kDownloadsDir);
    MigrateDirectory(downloads_in_myfiles, downloads);
  }

  return result;
}

std::vector<DirectoryACL> MountHelper::GetDmcryptSubdirectories(
    uid_t uid, gid_t gid, gid_t access_gid) {
  auto common_subdirs = GetCommonSubdirectories(uid, gid, access_gid);
  auto cache_subdirs = GetCacheSubdirectories(uid, gid, access_gid);
  auto gcache_subdirs =
      GetGCacheSubdirectories(uid, gid, access_gid, /*v1_dirs=*/true);

  // Construct data volume subdirectories.
  std::vector<DirectoryACL> data_volume_subdirs;
  data_volume_subdirs.insert(data_volume_subdirs.end(), common_subdirs.begin(),
                             common_subdirs.end());
  data_volume_subdirs.insert(data_volume_subdirs.end(), cache_subdirs.begin(),
                             cache_subdirs.end());

  for (auto& subdir : data_volume_subdirs) {
    subdir.path = FilePath(kMountDir).Append(subdir.path);
  }

  // Construct cache volume subdirectories.
  auto cache_volume_subdirs = cache_subdirs;
  cache_volume_subdirs.insert(cache_volume_subdirs.end(),
                              gcache_subdirs.begin(), gcache_subdirs.end());
  for (auto& subdir : cache_volume_subdirs) {
    subdir.path = FilePath(kDmcryptCacheDir).Append(subdir.path);
  }

  auto result = cache_volume_subdirs;
  result.insert(result.end(), data_volume_subdirs.begin(),
                data_volume_subdirs.end());
  return result;
}

bool MountHelper::CreateDmcryptSubdirectories(
    const std::string& obfuscated_username) {
  FilePath user_shadow_dir = ShadowRoot().Append(obfuscated_username);
  const std::vector<DirectoryACL> dmcrypt_subdirs =
      GetDmcryptSubdirectories(default_uid_, default_gid_, default_access_gid_);

  // Set up directories.
  for (const auto& subdir : dmcrypt_subdirs) {
    FilePath dir = user_shadow_dir.Append(subdir.path);
    // Ensure that the directory exists.
    if (!platform_->DirectoryExists(dir)) {
      // Delete the existing file or symbolic link if any.
      platform_->DeletePathRecursively(dir);
      VLOG(1) << "Creating directory " << dir.value();
      if (!platform_->SafeCreateDirAndSetOwnershipAndPermissions(
              dir, subdir.mode, subdir.uid, subdir.gid)) {
        PLOG(ERROR) << "SafeCreateDirAndSetOwnershipAndPermissions() failed: "
                    << dir.value();
        platform_->DeletePathRecursively(dir);
        return false;
      }
    }
  }

  return true;
}

bool MountHelper::MountCacheSubdirectories(
    const std::string& obfuscated_username) {
  FilePath cache_directory = GetDmcryptUserCacheDirectory(obfuscated_username);
  FilePath data_directory = GetUserMountDirectory(obfuscated_username);

  const FilePath tracked_subdir_paths[] = {
      FilePath(kUserHomeSuffix).Append(kCacheDir),
      FilePath(kUserHomeSuffix).Append(kGCacheDir)};

  for (const auto& tracked_dir : tracked_subdir_paths) {
    FilePath src_dir = cache_directory.Append(tracked_dir);
    FilePath dst_dir = data_directory.Append(tracked_dir);

    if (!BindAndPush(src_dir, dst_dir, RemountOption::kMountsFlowIn)) {
      LOG(ERROR) << "Failed to bind mount " << src_dir;
      return false;
    }
  }

  return true;
}

// The eCryptfs mount is mounted from vault/ --> mount/ except in case of
// migration where the mount point is a temporary directory.
bool MountHelper::SetUpEcryptfsMount(const std::string& obfuscated_username,
                                     const std::string& fek_signature,
                                     const std::string& fnek_signature,
                                     bool should_migrate) {
  const FilePath vault_path = GetEcryptfsUserVaultPath(obfuscated_username);
  const FilePath mount_point =
      should_migrate ? GetUserTemporaryMountDirectory(obfuscated_username)
                     : GetUserMountDirectory(obfuscated_username);

  // Specify the ecryptfs options for mounting the user's cryptohome.
  std::string ecryptfs_options = StringPrintf(
      "ecryptfs_cipher=aes"
      ",ecryptfs_key_bytes=%d"
      ",ecryptfs_fnek_sig=%s"
      ",ecryptfs_sig=%s"
      ",ecryptfs_unlink_sigs",
      kDefaultEcryptfsKeySize, fnek_signature.c_str(), fek_signature.c_str());

  // Create <vault_path>/user and <vault_path>/root.
  CreateHomeSubdirectories(vault_path);

  // Move the tracked subdirectories from <mount_point_>/user to <vault_path>
  // as passthrough directories.
  CreateTrackedSubdirectories(obfuscated_username, MountType::ECRYPTFS);

  // b/115997660: Mount eCryptfs after creating the tracked subdirectories.
  if (!MountAndPush(vault_path, mount_point, "ecryptfs", ecryptfs_options))
    return false;

  return true;
}

void MountHelper::SetUpDircryptoMount(const std::string& obfuscated_username) {
  const FilePath mount_point = GetUserMountDirectory(obfuscated_username);

  CreateHomeSubdirectories(mount_point);
  CreateTrackedSubdirectories(obfuscated_username, MountType::DIR_CRYPTO);
}

bool MountHelper::SetUpDmcryptMount(const std::string& obfuscated_username) {
  const FilePath dmcrypt_data_volume =
      GetDmcryptDataVolume(obfuscated_username);
  const FilePath dmcrypt_cache_volume =
      GetDmcryptCacheVolume(obfuscated_username);

  const FilePath data_mount_point = GetUserMountDirectory(obfuscated_username);
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

  CreateHomeSubdirectories(data_mount_point);
  CreateDmcryptSubdirectories(obfuscated_username);

  return true;
}

bool MountHelper::PerformMount(const Options& mount_opts,
                               const std::string& username,
                               const std::string& fek_signature,
                               const std::string& fnek_signature,
                               bool is_pristine,
                               MountError* error) {
  const std::string obfuscated_username = SanitizeUserName(username);

  bool should_mount_ecryptfs = mount_opts.type == MountType::ECRYPTFS ||
                               mount_opts.to_migrate_from_ecryptfs;

  if (should_mount_ecryptfs &&
      !SetUpEcryptfsMount(obfuscated_username, fek_signature, fnek_signature,
                          mount_opts.to_migrate_from_ecryptfs)) {
    LOG(ERROR) << "eCryptfs mount failed";
    *error = MOUNT_ERROR_MOUNT_ECRYPTFS_FAILED;
    return false;
  }

  if (mount_opts.type == MountType::DIR_CRYPTO)
    SetUpDircryptoMount(obfuscated_username);

  if (mount_opts.type == MountType::DMCRYPT &&
      !SetUpDmcryptMount(obfuscated_username)) {
    LOG(ERROR) << "Dm-crypt mount failed";
    *error = MOUNT_ERROR_MOUNT_DMCRYPT_FAILED;
    return false;
  }

  const FilePath user_home = GetMountedUserHomePath(obfuscated_username);
  const FilePath root_home = GetMountedRootHomePath(obfuscated_username);

  if (is_pristine)
    CopySkeleton(user_home);

  // When migrating, it's better to avoid exposing the new ext4 crypto dir.
  if (!mount_opts.to_migrate_from_ecryptfs &&
      !MountHomesAndDaemonStores(username, obfuscated_username, user_home,
                                 root_home)) {
    *error = MOUNT_ERROR_MOUNT_HOMES_AND_DAEMON_STORES_FAILED;
    return false;
  }

  // Mount tracked subdirectories from the cache volume.
  if (mount_opts.type == MountType::DMCRYPT &&
      !MountCacheSubdirectories(obfuscated_username)) {
    LOG(ERROR)
        << "Failed to mount tracked subdirectories from the cache volume";
    *error = MOUNT_ERROR_MOUNT_DMCRYPT_FAILED;
    return false;
  }

  return true;
}

bool MountHelper::PrepareEphemeralDevice(
    const std::string& obfuscated_username) {
  // Underlying sparse file will be created in a temporary directory in RAM.
  const FilePath ephemeral_root(kEphemeralCryptohomeDir);

  // Determine ephemeral cryptohome size.
  struct statvfs vfs;
  if (!platform_->StatVFS(ephemeral_root, &vfs)) {
    PLOG(ERROR) << "Can't determine ephemeral cryptohome size";
    return false;
  }
  const int64_t sparse_size = static_cast<int64_t>(vfs.f_blocks * vfs.f_frsize);

  // Create underlying sparse file.
  const FilePath sparse_file = GetEphemeralSparseFile(obfuscated_username);
  if (!platform_->CreateDirectory(sparse_file.DirName())) {
    LOG(ERROR) << "Can't create directory for ephemeral sparse files";
    return false;
  }

  // Remember the file to clean up if an error happens during file creation.
  ephemeral_file_path_ = sparse_file;
  if (!platform_->CreateSparseFile(sparse_file, sparse_size)) {
    LOG(ERROR) << "Can't create ephemeral sparse file";
    return false;
  }

  // Format the sparse file as ext4.
  if (!platform_->FormatExt4(sparse_file, kDefaultExt4FormatOpts, 0)) {
    LOG(ERROR) << "Can't format ephemeral sparse file as ext4";
    return false;
  }

  // Create a loop device based on the sparse file.
  const FilePath loop_device = platform_->AttachLoop(sparse_file);
  if (loop_device.empty()) {
    LOG(ERROR) << "Can't create loop device";
    return false;
  }

  // Remember the loop device to clean up if an error happens.
  ephemeral_loop_device_ = loop_device;
  return true;
}

bool MountHelper::PerformEphemeralMount(const std::string& username) {
  const std::string obfuscated_username =
      SanitizeUserNameWithSalt(username, system_salt_);

  if (!PrepareEphemeralDevice(obfuscated_username)) {
    LOG(ERROR) << "Can't prepare ephemeral device";
    return false;
  }

  const FilePath mount_point =
      GetUserEphemeralMountDirectory(obfuscated_username);
  if (!platform_->CreateDirectory(mount_point)) {
    PLOG(ERROR) << "Directory creation failed for " << mount_point.value();
    return false;
  }
  if (!MountAndPush(ephemeral_loop_device_, mount_point, kEphemeralMountType,
                    kEphemeralMountOptions)) {
    LOG(ERROR) << "Can't mount ephemeral mount point";
    return false;
  }

  // Set SELinux context first, so that the created user & root directory have
  // the correct context.
  if (!::SetUpSELinuxContextForEphemeralCryptohome(platform_, mount_point)) {
    // Logging already done in SetUpSELinuxContextForEphemeralCryptohome.
    return false;
  }

  // Create user & root directories.
  CreateHomeSubdirectories(mount_point);
  if (!EnsureUserMountPoints(username)) {
    return false;
  }

  const FilePath user_home =
      GetMountedEphemeralUserHomePath(obfuscated_username);
  const FilePath root_home =
      GetMountedEphemeralRootHomePath(obfuscated_username);

  if (!SetUpEphemeralCryptohome(mount_point)) {
    return false;
  }

  if (!MountHomesAndDaemonStores(username, obfuscated_username, user_home,
                                 root_home)) {
    return false;
  }

  return true;
}

bool MountHelper::TearDownEphemeralMount() {
  UnmountAll();
  return CleanUpEphemeral();
}

void MountHelper::TearDownNonEphemeralMount() {
  UnmountAll();
}

void MountHelper::UnmountAll() {
  FilePath src, dest;
  const FilePath ephemeral_mount_path =
      FilePath(kEphemeralCryptohomeDir).Append(kEphemeralMountDir);
  while (stack_.Pop(&src, &dest)) {
    ForceUnmount(src, dest);
    // Clean up destination directory for ephemeral loop device mounts.
    if (ephemeral_mount_path == dest.DirName())
      platform_->DeletePathRecursively(dest);
  }
}

bool MountHelper::CleanUpEphemeral() {
  bool success = true;
  if (!ephemeral_loop_device_.empty()) {
    if (!platform_->DetachLoop(ephemeral_loop_device_)) {
      PLOG(ERROR) << "Can't detach loop device '"
                  << ephemeral_loop_device_.value() << "'";
      success = false;
    }
    ephemeral_loop_device_.clear();
  }
  if (!ephemeral_file_path_.empty()) {
    if (!platform_->DeleteFile(ephemeral_file_path_)) {
      PLOG(ERROR) << "Failed to clean up ephemeral sparse file '"
                  << ephemeral_file_path_.value() << "'";
      success = false;
    }
    ephemeral_file_path_.clear();
  }

  return success;
}

void MountHelper::ForceUnmount(const FilePath& src, const FilePath& dest) {
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

bool MountHelper::CanPerformEphemeralMount() const {
  return ephemeral_file_path_.empty() && ephemeral_loop_device_.empty();
}

bool MountHelper::MountPerformed() const {
  return stack_.size() > 0;
}

bool MountHelper::IsPathMounted(const base::FilePath& path) const {
  return stack_.ContainsDest(path);
}

std::vector<base::FilePath> MountHelper::MountedPaths() const {
  return stack_.MountDestinations();
}

}  // namespace cryptohome
