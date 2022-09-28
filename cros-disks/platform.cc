// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/platform.h"

#include <dirent.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <iomanip>
#include <memory>
#include <vector>

#include <base/check.h>
#include <base/containers/contains.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/free_deleter.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/userdb_utils.h>

#include "cros-disks/quote.h"

namespace cros_disks {

Platform::Platform()
    : mount_group_id_(0), mount_user_id_(0), mount_user_("root") {}

bool Platform::GetRealPath(const std::string& path,
                           std::string* real_path) const {
  DCHECK(real_path);

  std::unique_ptr<char, base::FreeDeleter> result(
      realpath(path.c_str(), nullptr));
  if (!result) {
    PLOG(ERROR) << "Cannot get real path of " << redact(path);
    return false;
  }

  *real_path = result.get();
  VLOG(1) << "Real path of " << quote(path) << " is " << quote(*real_path);
  return true;
}

bool Platform::PathExists(const std::string& path) const {
  return base::PathExists(base::FilePath(path));
}

bool Platform::DirectoryExists(const std::string& path) const {
  return base::DirectoryExists(base::FilePath(path));
}

bool Platform::Lstat(const std::string& path, base::stat_wrapper_t* out) const {
  return base::File::Lstat(path.c_str(), out) == 0;
}

bool Platform::CreateDirectory(const std::string& path) const {
  if (!base::CreateDirectory(base::FilePath(path))) {
    PLOG(ERROR) << "Cannot create directory " << redact(path);
    return false;
  }

  VLOG(1) << "Created directory " << quote(path);
  return true;
}

bool Platform::CreateOrReuseEmptyDirectory(const std::string& path) const {
  DCHECK(!path.empty());

  // Reuse the target path if it already exists and is empty.
  // rmdir handles the cases when the target path exists but
  // is not empty, is already mounted or is used by some process.
  rmdir(path.c_str());
  if (mkdir(path.c_str(), S_IRWXU) != 0) {
    PLOG(ERROR) << "Cannot create directory " << redact(path);
    return false;
  }

  VLOG(1) << "Created directory " << quote(path);
  return true;
}

bool Platform::CreateOrReuseEmptyDirectoryWithFallback(
    std::string* path,
    unsigned max_suffix_to_retry,
    const std::unordered_set<std::string>& reserved_paths) const {
  DCHECK(path);
  DCHECK(!path->empty());

  if (!base::Contains(reserved_paths, *path) &&
      CreateOrReuseEmptyDirectory(*path))
    return true;

  for (unsigned suffix = 1; suffix <= max_suffix_to_retry; ++suffix) {
    std::string fallback_path = GetDirectoryFallbackName(*path, suffix);
    if (!base::Contains(reserved_paths, fallback_path) &&
        CreateOrReuseEmptyDirectory(fallback_path)) {
      *path = fallback_path;
      return true;
    }
  }

  return false;
}

bool Platform::CreateTemporaryDirInDir(const std::string& dir,
                                       const std::string& prefix,
                                       std::string* path) const {
  DCHECK(path);

  base::FilePath dest;
  if (!base::CreateTemporaryDirInDir(base::FilePath(dir), prefix, &dest)) {
    PLOG(ERROR) << "Cannot create temporary directory in " << quote(dir);
    return false;
  }

  VLOG(1) << "Created temporary directory " << quote(dest);
  *path = dest.value();

  return true;
}

int Platform::WriteFile(const std::string& file,
                        const char* data,
                        int size) const {
  return base::WriteFile(base::FilePath(file), data, size);
}

int Platform::ReadFile(const std::string& file, char* data, int size) const {
  return base::ReadFile(base::FilePath(file), data, size);
}

std::string Platform::GetDirectoryFallbackName(const std::string& path,
                                               unsigned suffix) const {
  if (!path.empty() && base::IsAsciiDigit(path.back()))
    return base::StringPrintf("%s (%u)", path.c_str(), suffix);

  return base::StringPrintf("%s %u", path.c_str(), suffix);
}

bool Platform::GetGroupId(const std::string& group_name,
                          gid_t* group_id) const {
  return brillo::userdb::GetGroupInfo(group_name, group_id);
}

bool Platform::GetUserAndGroupId(const std::string& user_name,
                                 uid_t* user_id,
                                 gid_t* group_id) const {
  return brillo::userdb::GetUserInfo(user_name, user_id, group_id);
}

bool Platform::GetOwnership(const std::string& path,
                            uid_t* user_id,
                            gid_t* group_id) const {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    PLOG(ERROR) << "Cannot get ownership info for " << quote(path);
    return false;
  }

  VLOG(1) << "File " << redact(path) << " has UID " << st.st_uid << " and GID "
          << st.st_gid;

  if (user_id)
    *user_id = st.st_uid;

  if (group_id)
    *group_id = st.st_gid;

  return true;
}

bool Platform::GetPermissions(const std::string& path, mode_t* mode) const {
  DCHECK(mode);

  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    PLOG(ERROR) << "Cannot get access mode of " << redact(path);
    return false;
  }

  VLOG(1) << "File " << redact(path) << " has access mode 0" << std::oct
          << std::setfill('0') << std::setw(3) << st.st_mode;
  *mode = st.st_mode;
  return true;
}

bool Platform::SetMountUser(const std::string& user_name) {
  if (GetUserAndGroupId(user_name, &mount_user_id_, &mount_group_id_)) {
    mount_user_ = user_name;
    return true;
  }
  return false;
}

bool Platform::RemoveEmptyDirectory(const std::string& path) const {
  if (rmdir(path.c_str()) == 0) {
    VLOG(1) << "Removed directory " << quote(path);
    return true;
  }

  if (errno == ENOENT) {
    VLOG(1) << "Tried to remove non-existent directory " << quote(path);
    return true;
  }

  PLOG(ERROR) << "Cannot remove directory " << redact(path);
  return false;
}

bool Platform::SetOwnership(const std::string& path,
                            uid_t user_id,
                            gid_t group_id) const {
  if (chown(path.c_str(), user_id, group_id)) {
    PLOG(ERROR) << "Cannot change ownership of " << quote(path) << " to UID "
                << user_id << " and GID " << group_id;
    return false;
  }

  VLOG(1) << "Changed ownership of " << quote(path) << " to UID " << user_id
          << " and GID " << group_id;
  return true;
}

bool Platform::SetPermissions(const std::string& path, mode_t mode) const {
  if (chmod(path.c_str(), mode)) {
    PLOG(ERROR) << "Cannot change access mode of " << quote(path) << " to 0"
                << std::oct << std::setfill('0') << std::setw(3) << mode;
    return false;
  }

  VLOG(1) << "Changed access mode of " << quote(path) << " to 0" << std::oct
          << std::setfill('0') << std::setw(3) << mode;
  return true;
}

MountErrorType Platform::Unmount(const base::FilePath& mount_path,
                                 const std::string& filesystem_type) const {
  // We take a 2-step approach to unmounting FUSE filesystems. First, we try a
  // normal unmount. This lets the VFS flush any pending data and lets the
  // filesystem shut down cleanly.
  //
  // However, if the filesystem is currently busy, this fails with an EBUSY
  // error.
  VLOG(2) << "Unmounting " << filesystem_type << " " << quote(mount_path);
  if (umount(mount_path.value().c_str()) == 0) {
    VLOG(1) << "Unmounted " << filesystem_type << " " << quote(mount_path);
    return MOUNT_ERROR_NONE;
  }

  if (errno == EBUSY) {
    // The normal unmount failed because the filesystem is busy. We now try to
    // force-unmount the filesystem. This is done because there is no good
    // recovery path the user can take, and these filesystems are sometimes
    // unmounted implicitly on login/logout/suspend.
    //
    // For FUSE filesystems, MNT_FORCE causes the kernel driver to immediately
    // close the channel to the user-space driver program and cancel all
    // outstanding requests. However, if any program was still accessing the
    // filesystem, the umount2(..., MNT_FORCE) would fail with EBUSY and the
    // mountpoint would still be attached. Since the mountpoint is no longer
    // valid, we also use MNT_DETACH to force the mountpoint to be disconnected.
    //
    // On a non-FUSE filesystem, MNT_FORCE doesn't have any effect. Only
    // MNT_DETACH matters in this case, but it's OK to pass MNT_FORCE too.
    VLOG(1) << "Force-unmounting " << filesystem_type << " "
            << quote(mount_path);
    if (umount2(mount_path.value().c_str(), MNT_FORCE | MNT_DETACH) == 0) {
      LOG(WARNING) << "Force-unmounted " << filesystem_type << " "
                   << redact(mount_path);
      return MOUNT_ERROR_NONE;
    }
  }

  const error_t error = errno;
  PLOG(ERROR) << "Cannot unmount " << filesystem_type << " "
              << redact(mount_path);

  switch (error) {
    case EINVAL:  // |mount_path| is not a mount point
    case ENOENT:  // |mount_path| has a nonexistent component
      return MOUNT_ERROR_PATH_NOT_MOUNTED;
    case EPERM:
      return MOUNT_ERROR_INSUFFICIENT_PERMISSIONS;
    case EBUSY:  // This should not happen since we force-unmount
      return MOUNT_ERROR_PATH_ALREADY_MOUNTED;
    default:
      return MOUNT_ERROR_UNKNOWN;
  }
}

MountErrorType Platform::Mount(const std::string& source_path,
                               const std::string& target_path,
                               const std::string& filesystem_type,
                               const uint64_t flags,
                               const std::string& options) const {
  if (mount(source_path.c_str(), target_path.c_str(), filesystem_type.c_str(),
            flags, options.c_str()) == 0) {
    VLOG(1) << "Created mount point " << quote(target_path) << " for "
            << quote(source_path) << " as filesystem " << quote(filesystem_type)
            << " with flags 0x" << std::hex << flags << " and options "
            << quote(options);
    return MOUNT_ERROR_NONE;
  }

  const error_t error = errno;
  PLOG(ERROR) << "Cannot create mount point " << redact(target_path) << " for "
              << redact(source_path) << " as filesystem "
              << quote(filesystem_type) << " with flags 0x" << std::hex << flags
              << " and options " << quote(options);

  switch (error) {
    case ENODEV:
      return MOUNT_ERROR_UNSUPPORTED_FILESYSTEM;
    case ENOENT:
    case ENOTBLK:
    case ENOTDIR:
      return MOUNT_ERROR_INVALID_PATH;
    case EPERM:
      return MOUNT_ERROR_INSUFFICIENT_PERMISSIONS;
    default:
      return MOUNT_ERROR_UNKNOWN;
  }
}

bool Platform::CleanUpStaleMountPoints(const std::string& dir) const {
  // We cannot use base::FileEnumerator here because FileEnumerator tries to
  // call `stat` on the found entries, and this fails if the entry is a FUSE
  // mount point for which the FUSE daemon is already dead.
  struct CloseDir {
    void operator()(DIR* const p) const { closedir(p); }
  };

  const std::unique_ptr<DIR, CloseDir> d(opendir(dir.c_str()));
  if (!d) {
    PLOG(ERROR) << "Cannot enumerate entries in " << quote(dir);
    return false;
  }

  while (true) {
    errno = 0;
    const dirent* const entry = readdir(d.get());
    if (!entry)
      break;

    const base::StringPiece name = entry->d_name;
    if (name == "." || name == "..")
      continue;

    const base::FilePath subdir = base::FilePath(dir).Append(name);
    LOG(WARNING) << "Found stale mount point " << redact(subdir);

    if (Platform::Unmount(subdir, "stale") == MOUNT_ERROR_NONE)
      LOG(WARNING) << "Unmounted stale mount point " << redact(subdir);

    if (Platform::RemoveEmptyDirectory(subdir.value()))
      LOG(WARNING) << "Removed stale mount point " << redact(subdir);
  }

  if (errno) {
    PLOG(ERROR) << "Error while enumerating entries in " << quote(dir);
    return false;
  }

  return true;
}

}  // namespace cros_disks
