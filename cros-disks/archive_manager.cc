// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/archive_manager.h"

#include <linux/capability.h>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/cryptohome.h>

#include "cros-disks/metrics.h"
#include "cros-disks/mount_info.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"
#include "cros-disks/sandboxed_process.h"
#include "cros-disks/system_mounter.h"

using base::FilePath;
using std::map;
using std::string;
using std::vector;

namespace {

// Mapping from a base path to its corresponding path inside the AVFS mount.
struct AVFSPathMapping {
  const char* const base_path;
  const char* const avfs_path;
};

// Process capabilities required by the avfsd process:
//   CAP_SYS_ADMIN for mounting/unmounting filesystem
const uint64_t kAVFSMountProgramCapabilities = 1 << CAP_SYS_ADMIN;

const char kAVFSMountGroup[] = "chronos-access";
const char kAVFSMountUser[] = "avfs";
// TODO(wad,benchan): Revisit the location of policy files once more system
// daemons are sandboxed with seccomp filters.
const char kAVFSSeccompFilterPolicyFile[] =
    "/opt/google/cros-disks/avfsd-seccomp.policy";
const char kAVFSMountProgram[] = "/usr/bin/avfsd";
const char kAVFSRootDirectory[] = "/run/avfsroot";
const char kAVFSLogFile[] = "/run/avfsroot/avfs.log";
const char kAVFSMediaDirectory[] = "/run/avfsroot/media";
const char kAVFSUsersDirectory[] = "/run/avfsroot/users";
const char kMediaDirectory[] = "/media";
const char kUserRootDirectory[] = "/home/chronos";
const AVFSPathMapping kAVFSPathMapping[] = {
  { kMediaDirectory, kAVFSMediaDirectory },
  { kUserRootDirectory, kAVFSUsersDirectory },
};

}  // namespace

namespace cros_disks {

ArchiveManager::ArchiveManager(const string& mount_root,
                               Platform* platform,
                               Metrics* metrics)
    : MountManager(mount_root, platform, metrics),
      avfs_started_(false) {
}

ArchiveManager::~ArchiveManager() {
  // StopAVFS() unmounts all mounted archives as well as AVFS mount points.
  StopAVFS();
}

bool ArchiveManager::Initialize() {
  RegisterDefaultFileExtensions();
  return MountManager::Initialize();
}

bool ArchiveManager::StopSession() {
  return StopAVFS();
}

bool ArchiveManager::CanMount(const string& source_path) const {
  // The following paths can be mounted:
  //     /home/chronos/u-<user-id>/Downloads/...<file>
  //     /home/chronos/u-<user-id>/GCache/...<file>
  //     /media/<dir>/<dir>/...<file>
  //
  FilePath file_path(source_path);
  if (FilePath(kUserRootDirectory).IsParent(file_path)) {
    vector<FilePath::StringType> components;
    file_path.StripTrailingSeparators().GetComponents(&components);
    // The file path of an archive file under a user's Downloads or GCache
    // directory path is split into the following components:
    //   '/', 'home', 'chronos', 'u-<userid>', 'Downloads', ..., 'doc.zip'
    //   '/', 'home', 'chronos', 'u-<userid>', 'GCache', ..., 'doc.zip'
    if (components.size() > 5 &&
        (StartsWithASCII(components[3], "u-", false) &&
         chromeos::cryptohome::home::IsSanitizedUserName(
             components[3].substr(2))) &&
        (components[4] == "Downloads" || components[4] == "GCache")) {
      return true;
    }
  }

  if (FilePath(kMediaDirectory).IsParent(file_path)) {
    vector<FilePath::StringType> components;
    file_path.StripTrailingSeparators().GetComponents(&components);
    // A mount directory is always created under /media/<sub type>/<mount dir>,
    // so the file path of an archive file under a mount directory is split
    // into more than 4 components:
    //   '/', 'media', 'removable', 'usb', ..., 'doc.zip'
    if (components.size() > 4)
      return true;
  }
  return false;
}

MountErrorType ArchiveManager::DoMount(const string& source_path,
                                       const string& source_format,
                                       const vector<string>& options,
                                       const string& mount_path) {
  CHECK(!source_path.empty()) << "Invalid source path argument";
  CHECK(!mount_path.empty()) << "Invalid mount path argument";

  string extension = GetFileExtension(source_format);
  if (extension.empty())
    extension = GetFileExtension(source_path);

  metrics()->RecordArchiveType(extension);

  string avfs_path = GetAVFSPath(source_path, extension);
  if (avfs_path.empty()) {
    LOG(ERROR) << "Path '" << source_path << "' is not a supported archive";
    return MOUNT_ERROR_UNSUPPORTED_ARCHIVE;
  }

  if (!StartAVFS()) {
    LOG(ERROR) << "Failed to start AVFS mounts.";
    return MOUNT_ERROR_INTERNAL;
  }

  // Perform a bind mount from the archive path under the AVFS mount
  // to /media/archive/<archive name>.
  vector<string> extended_options = options;
  extended_options.push_back(MountOptions::kOptionBind);
  MountOptions mount_options;
  mount_options.Initialize(extended_options, false, "", "");
  SystemMounter mounter(avfs_path, mount_path, "", mount_options);

  MountErrorType error_type = mounter.Mount();
  if (error_type == MOUNT_ERROR_NONE) {
    AddMountVirtualPath(mount_path, avfs_path);
  }
  return error_type;
}

MountErrorType ArchiveManager::DoUnmount(const string& path,
                                         const vector<string>& options) {
  CHECK(!path.empty()) << "Invalid path argument";
  // TODO(benchan): Extract error from low-level unmount operation.
  if (platform()->Unmount(path)) {
    // DoUnmount() is always called with |path| being the mount path.
    RemoveMountVirtualPath(path);
    return MOUNT_ERROR_NONE;
  }
  return MOUNT_ERROR_UNKNOWN;
}

string ArchiveManager::SuggestMountPath(const string& source_path) const {
  // Use the archive name to name the mount directory.
  FilePath base_name = FilePath(source_path).BaseName();
  return FilePath(mount_root()).Append(base_name).value();
}

void ArchiveManager::RegisterDefaultFileExtensions() {
  // TODO(benchan): Perhaps these settings can be read from a config file.

  // zip
  RegisterFileExtension("zip", "#uzip");
  // tar
  RegisterFileExtension("tar", "#utar");
  // All variants of bzip2-compessed tar
  RegisterFileExtension("tar.bz2", "#ubz2#utar");
  RegisterFileExtension("tbz", "#ubz2#utar");
  RegisterFileExtension("tbz2", "#ubz2#utar");
  // All variants of gzip-compessed tar
  RegisterFileExtension("tar.gz", "#ugz#utar");
  RegisterFileExtension("tgz", "#ugz#utar");
  // rar
  RegisterFileExtension("rar", "#urar");
}

void ArchiveManager::RegisterFileExtension(const string& extension,
                                           const string& avfs_handler) {
  extension_handlers_[extension] = avfs_handler;
}

string ArchiveManager::GetFileExtension(const string& path) const {
  FilePath file_path(path);
  string extension = file_path.Extension();
  if (!extension.empty()) {
    // Strip the leading dot and convert the extension to lower case.
    extension.erase(0, 1);
    base::StringToLowerASCII(&extension);
  }
  return extension;
}

string ArchiveManager::GetAVFSPath(const string& path,
                                   const string& extension) const {
  // When mounting an archive within another mounted archive, we need to
  // resolve the virtual path of the inner archive to the "unfolded"
  // form within the AVFS mount, such as
  //   "/run/avfsroot/media/layer2.zip#/test/doc/layer1.zip#"
  // instead of the "nested" form, such as
  //   "/run/avfsroot/media/archive/layer2.zip/test/doc/layer1.zip#"
  // where "/media/archive/layer2.zip" is a mount point to the virtual
  // path "/run/avfsroot/media/layer2.zip#".
  //
  // Mounting the inner archive using the nested form may cause problems
  // reading files from the inner archive. To avoid that, we first try to
  // find the longest parent path of |path| that is an existing mount
  // point to a virtual path within the AVFS mount. If such a parent path
  // is found, we construct the virtual path of |path| within the AVFS
  // mount as a subpath of its parent's virtual path.
  //
  // e.g. Given |path| is "/media/archive/layer2.zip/test/doc/layer1.zip",
  //      and "/media/archive/layer2.zip" is a mount point to the virtual
  //      path "/run/avfsroot/media/layer2.zip#" within the AVFS mount.
  //      The following code should return the virtual path of |path| as
  //      "/run/avfsroot/media/layer2.zip#/test/doc/layer1.zip#".
  map<string, string>::const_iterator handler_iterator =
      extension_handlers_.find(extension);
  if (handler_iterator == extension_handlers_.end())
    return string();

  FilePath file_path(path);
  FilePath current_path = file_path.DirName();
  FilePath parent_path = current_path.DirName();
  while (current_path != parent_path) {  // Search till the root
    VirtualPathMap::const_iterator path_iterator =
        virtual_paths_.find(current_path.value());
    if (path_iterator != virtual_paths_.end()) {
      FilePath avfs_path(path_iterator->second);
      // As current_path is a parent of file_path, AppendRelativePath()
      // should return true here.
      CHECK(current_path.AppendRelativePath(file_path, &avfs_path));
      return avfs_path.value() + handler_iterator->second;
    }
    current_path = parent_path;
    parent_path = parent_path.DirName();
  }

  // If no parent path is a mounted via AVFS, we are not mounting a nested
  // archive and thus construct the virtual path of the archive based on a
  // corresponding AVFS mount path.
  for (size_t i = 0; i < arraysize(kAVFSPathMapping); ++i) {
    FilePath base_path(kAVFSPathMapping[i].base_path);
    FilePath avfs_path(kAVFSPathMapping[i].avfs_path);
    if (base_path.AppendRelativePath(file_path, &avfs_path)) {
      return avfs_path.value() + handler_iterator->second;
    }
  }
  return string();
}

bool ArchiveManager::StartAVFS() {
  if (avfs_started_)
    return true;

  uid_t user_id;
  gid_t group_id;
  if (!platform()->GetUserAndGroupId(kAVFSMountUser, &user_id, &group_id) ||
      !platform()->CreateDirectory(kAVFSRootDirectory) ||
      !platform()->SetOwnership(kAVFSRootDirectory, user_id, group_id) ||
      !platform()->SetPermissions(kAVFSRootDirectory, S_IRWXU)) {
    platform()->RemoveEmptyDirectory(kAVFSRootDirectory);
    return false;
  }

  // Set the AVFS_LOGFILE environment variable so that the AVFS daemon
  // writes log messages to a file instead of syslog. Otherwise, writing
  // to syslog may trigger the socket/connect/send system calls, which are
  // disabled by the seccomp filters policy file. This only affects the
  // child processes spawned by cros-disks and does not persist after
  // cros-disks restarts.
  setenv("AVFS_LOGFILE", kAVFSLogFile, 1);

  avfs_started_ = true;
  for (size_t i = 0; i < arraysize(kAVFSPathMapping); ++i) {
    bool base_path_exists =
        base::PathExists(FilePath(kAVFSPathMapping[i].base_path));
    const string& avfs_path = kAVFSPathMapping[i].avfs_path;
    if (!base_path_exists ||
        !platform()->CreateDirectory(avfs_path) ||
        !platform()->SetOwnership(avfs_path, user_id, group_id) ||
        !platform()->SetPermissions(avfs_path, S_IRWXU) ||
        !MountAVFSPath(kAVFSPathMapping[i].base_path, avfs_path)) {
      StopAVFS();
      return false;
    }
  }
  return true;
}

bool ArchiveManager::StopAVFS() {
  if (!avfs_started_)
    return true;

  avfs_started_ = false;
  // Unmounts all mounted archives before unmounting AVFS mounts.
  bool all_unmounted = UnmountAll();
  for (size_t i = 0; i < arraysize(kAVFSPathMapping); ++i) {
    const string& path = kAVFSPathMapping[i].avfs_path;
    if (!base::PathExists(FilePath(path)))
      continue;

    if (!platform()->Unmount(path))
      all_unmounted = false;
    platform()->RemoveEmptyDirectory(path);
  }
  platform()->RemoveEmptyDirectory(kAVFSRootDirectory);
  return all_unmounted;
}

bool ArchiveManager::MountAVFSPath(const string& base_path,
                                   const string& avfs_path) const {
  MountInfo mount_info;
  if (!mount_info.RetrieveFromCurrentProcess())
    return false;

  if (mount_info.HasMountPath(avfs_path)) {
    LOG(WARNING) << "Path '" << avfs_path << "' is already mounted.";
    return false;
  }

  uid_t user_id;
  gid_t group_id;
  if (!platform()->GetUserAndGroupId(kAVFSMountUser, &user_id, nullptr) ||
      !platform()->GetGroupId(kAVFSMountGroup, &group_id)) {
    return false;
  }

  SandboxedProcess mount_process;
  mount_process.AddArgument(kAVFSMountProgram);
  mount_process.AddArgument("-o");
  mount_process.AddArgument(base::StringPrintf(
      "ro,nodev,noexec,nosuid,allow_other,user=%s,modules=subdir,subdir=%s",
      kAVFSMountUser, base_path.c_str()));
  mount_process.AddArgument(avfs_path);
  if (base::PathExists(FilePath(kAVFSSeccompFilterPolicyFile))) {
    mount_process.LoadSeccompFilterPolicy(kAVFSSeccompFilterPolicyFile);
  } else {
    // TODO(benchan): Remove this fallback mechanism once we have policy files
    //                for all supported platforms.
    LOG(WARNING) << "Seccomp filter policy '" << kAVFSSeccompFilterPolicyFile
                 << "' not found. Use POSIX capabilities mechanism instead";
    mount_process.SetCapabilities(kAVFSMountProgramCapabilities);
  }
  // TODO(benchan): Enable PID and VFS namespace.
  // TODO(wad,ellyjones,benchan): Enable network namespace once libminijail
  // supports it.
  mount_process.SetUserId(user_id);
  mount_process.SetGroupId(group_id);
  if (mount_process.Run() != 0 ||
      !mount_info.RetrieveFromCurrentProcess() ||
      !mount_info.HasMountPath(avfs_path)) {
    LOG(WARNING) << "Failed to mount '" << base_path << "' to '"
                 << avfs_path << "' via AVFS";
    return false;
  }

  LOG(INFO) << "Mounted '" << base_path << "' to '" << avfs_path
            << "' via AVFS";
  return true;
}

void ArchiveManager::AddMountVirtualPath(const string& mount_path,
                                         const string& virtual_path) {
  virtual_paths_[mount_path] = virtual_path;
}

void ArchiveManager::RemoveMountVirtualPath(const string& mount_path) {
  virtual_paths_.erase(mount_path);
}

}  // namespace cros_disks
