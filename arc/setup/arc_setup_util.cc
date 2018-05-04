// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/setup/arc_setup_util.h"

#include <fcntl.h>
#include <limits.h>
#include <linux/loop.h>
#include <linux/major.h>
#include <mntent.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <selinux/restorecon.h>
#include <selinux/selinux.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <list>
#include <set>
#include <utility>

#include <base/bind.h>
#include <base/environment.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/process/launch.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <base/timer/elapsed_timer.h>
#include <base/values.h>
#include <chromeos-config/libcros_config/cros_config.h>
#include <crypto/sha2.h>

namespace arc {

namespace {

// The path in the chromeos-config database where Android properties will be
// looked up.
constexpr char kCrosConfigPropertiesPath[] = "/arc/build-properties";

// Version element prefix in packages.xml and packages_cache.xml files.
constexpr char kElementVersion[] = "<version ";

// Fingerprint attribute prefix in packages.xml and packages_cache.xml files.
constexpr char kAttributeFingerprint[] = " fingerprint=\"";

// Maximum length of an Android property value.
constexpr int kAndroidMaxPropertyLength = 91;

std::string GetLoopDevice(int32_t device) {
  return base::StringPrintf("/dev/loop%d", device);
}

// A callback function for SELinux restorecon.
PRINTF_FORMAT(2, 3)
int RestoreConLogCallback(int type, const char* fmt, ...) {
  va_list ap;

  std::string message = "restorecon: ";
  va_start(ap, fmt);
  message += base::StringPrintV(fmt, ap);
  va_end(ap);

  // This already has a line feed at the end, so trim it off to avoid
  // empty lines in the log.
  base::TrimString(message, "\r\n", &message);

  if (type == SELINUX_INFO)
    LOG(INFO) << message;
  else
    LOG(ERROR) << message;

  return 0;
}

bool RestoreconInternal(const std::vector<base::FilePath>& paths,
                        bool is_recursive) {
  union selinux_callback cb;
  cb.func_log = RestoreConLogCallback;
  selinux_set_callback(SELINUX_CB_LOG, cb);

  const unsigned int restorecon_flags =
      (is_recursive ? SELINUX_RESTORECON_RECURSE : 0) |
      SELINUX_RESTORECON_REALPATH;

  bool success = true;
  for (const auto& path : paths) {
    if (selinux_restorecon(path.value().c_str(), restorecon_flags) != 0) {
      LOG(ERROR) << "Error in restorecon of " << path.value();
      success = false;
    }
  }
  return success;
}

// A callback function for GetPropertyFromFile.
bool FindProperty(const std::string& line_prefix_to_find,
                  const std::string& line,
                  std::string* out_prop) {
  if (base::StartsWith(line, line_prefix_to_find,
                       base::CompareCase::SENSITIVE)) {
    *out_prop = line.substr(line_prefix_to_find.length());
    return true;
  }
  return false;
}

// A callback function for GetFingerprintFromPackagesXml. This checks if the
// |line| is like
//    <version sdkVersion="25" databaseVersion="3" fingerprint="..." />
// and store the fingerprint part in |out_fingerprint| if it is. Ignore a line
// with a volumeUuid attribute which means that the line is for an external
// storage. What we need is a fingerprint for an internal storage.
bool FindFingerprint(const std::string& line, std::string* out_fingerprint) {
  constexpr char kAttributeVolumeUuid[] = " volumeUuid=\"";
  constexpr char kAttributeSdkVersion[] = " sdkVersion=\"";
  constexpr char kAttributeDatabaseVersion[] = " databaseVersion=\"";

  // Parsing an XML this way is not very clean but in this case, it works (and
  // fast.) Android's packages.xml is written in com.android.server.pm.Settings'
  // writeLPr(), and the write function always uses Android's FastXmlSerializer.
  // The serializer does not try to pretty-print the XML, and inserts '\n' only
  // to certain places like endTag.
  // TODO(yusukes): Check Android P's writeLPr() and FastXmlSerializer although
  // they unlikely change.
  std::string trimmed;
  base::TrimWhitespaceASCII(line, base::TRIM_ALL, &trimmed);
  if (!base::StartsWith(trimmed, kElementVersion, base::CompareCase::SENSITIVE))
    return false;  // Not a <version> element. Ignoring.

  if (trimmed.find(kAttributeVolumeUuid) != std::string::npos)
    return false;  // This is for an external storage. Ignoring.

  std::string::size_type pos = trimmed.find(kAttributeFingerprint);
  if (pos == std::string::npos ||
      // Do some more sanity checks.
      trimmed.find(kAttributeSdkVersion) == std::string::npos ||
      trimmed.find(kAttributeDatabaseVersion) == std::string::npos) {
    LOG(WARNING) << "Unexpected <version> format: " << trimmed;
    return false;
  }

  // Found the line we need.
  std::string fingerprint = trimmed.substr(pos + strlen(kAttributeFingerprint));
  pos = fingerprint.find('"');
  if (pos == std::string::npos) {
    LOG(WARNING) << "<version> doesn't have a valid fingerprint: " << trimmed;
    return false;
  }

  *out_fingerprint = fingerprint.substr(0, pos);
  return true;
}

// Reads |file_path| line by line and pass each line to the |callback| after
// trimming it. |out_string| is also passed to the |callback| each time. If
// |callback| returns true, stops reading the file and returns true. |callback|
// must update |out_string| before returning true.
bool FindLine(
    const base::FilePath& file_path,
    const base::Callback<bool(const std::string&, std::string*)>& callback,
    std::string* out_string) {
  // Do exactly the same stream handling as TextContentsEqual() in
  // base/files/file_util.cc which is known to work.
  std::ifstream file(file_path.value().c_str(), std::ios::in);
  if (!file.is_open()) {
    PLOG(WARNING) << "Cannot open " << file_path.value();
    return false;
  }

  do {
    std::string line;
    std::getline(file, line);

    // Check for any error state.
    if (file.bad()) {
      PLOG(WARNING) << "Failed to read " << file_path.value();
      return false;
    }

    // Trim all '\r' and '\n' characters from the end of the line.
    std::string::size_type end = line.find_last_not_of("\r\n");
    if (end == std::string::npos)
      line.clear();
    else if (end + 1 < line.length())
      line.erase(end + 1);

    // Stop reading the file if |callback| returns true.
    if (callback.Run(line, out_string))
      return true;
  } while (!file.eof());

  // |callback| didn't find anything in the file.
  return false;
}

// Sets the permission of the given |fd|.
bool SetPermissions(base::PlatformFile fd, mode_t mode) {
  struct stat st;
  if (fstat(fd, &st) < 0) {
    PLOG(ERROR) << "Failed to stat";
    return false;
  }
  if ((st.st_mode & 07000) && ((st.st_mode & 07000) != (mode & 07000))) {
    LOG(INFO) << "Changing permissions from " << (st.st_mode & ~S_IFMT)
              << " to " << (mode & ~S_IFMT);
  }

  if (fchmod(fd, mode) != 0) {
    PLOG(ERROR) << "Failed to fchmod " << mode;
    return false;
  }
  return true;
}

// Opens the |path| with safety checks and returns a FD. This function returns
// an invalid FD if open() fails or the returned fd is not safe for use. The
// function also returns an invalid FD when |path| is relative. |mode| is
// ignored unless |flags| has either O_CREAT or O_TMPFILE.
// TODO(yusukes): Consider moving this to libbrillo. Add HANDLE_EINTR and
// O_CLOEXEC when doing that.
base::ScopedFD OpenSafelyInternal(const base::FilePath& path,
                                  int flags,
                                  mode_t mode) {
  if (!path.IsAbsolute()) {
    LOG(INFO) << "Relative paths are not supported: " << path.value();
    return base::ScopedFD();
  }

  base::ScopedFD fd(
      open(path.value().c_str(), flags | O_NOFOLLOW | O_NONBLOCK, mode));
  if (!fd.is_valid()) {
    // open(2) fails with ELOOP whtn the last component of the |path| is a
    // symlink. It fails with ENXIO when |path| is a FIFO and |flags| is for
    // writing because of the O_NONBLOCK flag added above.
    if (errno == ELOOP || errno == ENXIO)
      PLOG(WARNING) << "Failed to open " << path.value() << " safely.";
    return base::ScopedFD();
  }

  // Finally, check if there are symlink(s) in other path components.
  const base::FilePath proc_fd(
      base::StringPrintf("/proc/self/fd/%d", fd.get()));
  base::FilePath resolved;
  if (!base::ReadSymbolicLink(proc_fd, &resolved)) {
    LOG(ERROR) << "Failed to read " << proc_fd.value();
    return base::ScopedFD();
  }
  // Note: |path| has to be absolute to pass this check.
  if (resolved != path) {
    LOG(ERROR) << "Symbolic link detected in " << path.value()
               << ". Resolved path=" << resolved.value();
    return base::ScopedFD();
  }

  // Remove the O_NONBLOCK flag unless the orignal |flags| have it.
  if ((flags & O_NONBLOCK) == 0) {
    flags = fcntl(fd.get(), F_GETFL);
    if (flags == -1) {
      PLOG(ERROR) << "Failed to get fd flags for " << path.value();
      return base::ScopedFD();
    }
    if (fcntl(fd.get(), F_SETFL, flags & ~O_NONBLOCK)) {
      PLOG(ERROR) << "Failed to set fd flags for " << path.value();
      return base::ScopedFD();
    }
  }

  return fd;
}

// Calls OpenSafelyInternal() and checks if the returned FD is for a regular
// file or directory. Returns an invalid FD if it's not.
base::ScopedFD OpenSafely(const base::FilePath& path, int flags, mode_t mode) {
  base::ScopedFD fd(OpenSafelyInternal(path, flags, mode));
  if (!fd.is_valid())
    return base::ScopedFD();

  // Ensure the opened file is a regular file or directory.
  struct stat st;
  if (fstat(fd.get(), &st) < 0) {
    PLOG(ERROR) << "Failed to fstat " << path.value();
    return base::ScopedFD();
  }

  if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
    // This detects a FIFO opened for reading, for example.
    LOG(ERROR) << path.value()
               << " is not a regular file/directory: " << st.st_mode;
    return base::ScopedFD();
  }

  return fd;
}

class ArcMounterImpl : public ArcMounter {
 public:
  ArcMounterImpl() = default;
  ~ArcMounterImpl() override = default;

  bool Mount(const std::string& source,
             const base::FilePath& target,
             const char* filesystem_type,
             unsigned long mount_flags,  // NOLINT(runtime/int)
             const char* data) override {
    std::string source_resolved;
    if (!source.empty() && source[0] == '/')
      source_resolved = Realpath(base::FilePath(source)).value();
    else
      source_resolved = source;  // not a path (e.g. "tmpfs")

    if (mount(source_resolved.c_str(), Realpath(target).value().c_str(),
              filesystem_type, mount_flags, data) != 0) {
      PLOG(ERROR) << "Failed to mount " << source << " to " << target.value();
      return false;
    }
    return true;
  }

  bool Remount(const base::FilePath& target_directory,
               unsigned long mount_flags,  // NOLINT(runtime/int)
               const char* data) override {
    return Mount(std::string(),  // ignored
                 target_directory,
                 nullptr,  // ignored
                 mount_flags | MS_REMOUNT, data);
  }

  bool LoopMount(const std::string& source,
                 const base::FilePath& target,
                 unsigned long mount_flags) override {  // NOLINT(runtime/int)
    constexpr size_t kRetryMax = 10;
    for (size_t i = 0; i < kRetryMax; ++i) {
      bool retry = false;
      if (LoopMountInternal(source, target, mount_flags, &retry))
        return true;
      if (!retry)
        break;
      LOG(INFO) << "LoopMountInternal failed with EBUSY. Retrying...";
    }
    return false;
  }

  bool BindMount(const base::FilePath& old_path,
                 const base::FilePath& new_path) override {
    return Mount(old_path.value(), new_path, nullptr, MS_BIND, nullptr);
  }

  bool SharedMount(const base::FilePath& path) override {
    return Mount("none", path, nullptr, MS_SHARED, nullptr);
  }

  bool Umount(const base::FilePath& path) override {
    if (umount(Realpath(path).value().c_str()) != 0) {
      PLOG(ERROR) << "Failed to umount " << path.value();
      return false;
    }
    return true;
  }

  bool UmountLazily(const base::FilePath& path) override {
    if (umount2(Realpath(path).value().c_str(), MNT_DETACH) != 0) {
      PLOG(ERROR) << "Failed to lazy-umount " << path.value();
      return false;
    }
    return true;
  }

  bool LoopUmount(const base::FilePath& path) override {
    struct stat st;
    if (stat(path.value().c_str(), &st) < 0) {
      PLOG(ERROR) << "Failed to stat " << path.value();
      return false;
    }

    if (!Umount(path))
      return false;

    if (major(st.st_dev) != LOOP_MAJOR) {
      LOG(ERROR) << path.value()
                 << " is not loop-mounted. st_dev=" << st.st_dev;
      return false;
    }

    const std::string device_file = GetLoopDevice(minor(st.st_dev));
    const int fd = open(device_file.c_str(), O_RDWR);
    if (fd < 0) {
      PLOG(ERROR) << "Failed to open " << device_file;
      return false;
    }
    base::ScopedFD scoped_loop_fd(fd);

    if (ioctl(scoped_loop_fd.get(), LOOP_CLR_FD)) {
      PLOG(ERROR) << "Failed to free " << device_file;
      return false;
    }
    return true;
  }

 private:
  bool LoopMountInternal(const std::string& source,
                         const base::FilePath& target,
                         unsigned long mount_flags,  // NOLINT(runtime/int)
                         bool* out_retry) {
    constexpr char kLoopControl[] = "/dev/loop-control";

    *out_retry = false;
    int fd = open(kLoopControl, O_RDONLY);
    if (fd < 0) {
      PLOG(ERROR) << "Failed to open " << kLoopControl;
      return false;
    }
    base::ScopedFD scoped_control_fd(fd);

    const int32_t device_num =
        ioctl(scoped_control_fd.get(), LOOP_CTL_GET_FREE);
    if (device_num < 0) {
      PLOG(ERROR) << "Failed to allocate a loop device";
      return false;
    }

    const std::string device_file = GetLoopDevice(device_num);
    fd = open(device_file.c_str(), O_RDWR);
    if (fd < 0) {
      PLOG(ERROR) << "Failed to open " << device_file;
      return false;
    }
    base::ScopedFD scoped_loop_fd(fd);

    const bool is_readonly_mount = mount_flags & MS_RDONLY;
    fd = open(source.c_str(), is_readonly_mount ? O_RDONLY : O_RDWR);
    if (fd < 0) {
      // If the open failed because we tried to open a read only file as RW
      // we fallback to opening it with O_RDONLY
      if (!is_readonly_mount && (errno == EROFS || errno == EACCES)) {
        LOG(WARNING) << source << " is write-protected, using read-only";
        fd = open(source.c_str(), O_RDONLY);
      }
      if (fd < 0) {
        PLOG(ERROR) << "Failed to open " << source;
        return false;
      }
    }
    base::ScopedFD scoped_source_fd(fd);

    if (ioctl(scoped_loop_fd.get(), LOOP_SET_FD, scoped_source_fd.get()) < 0) {
      PLOG(ERROR) << "Failed to associate " << source << " with "
                  << device_file;
      // Set |out_retry| to true if LOOP_SET_FD returns EBUSY. The errno
      // indicates that another process has grabbed the same |device_num|
      // before arc-setup does that.
      *out_retry = (errno == EBUSY);
      return false;
    }

    if (Mount(device_file, target, "squashfs", mount_flags, nullptr))
      return true;

    // For debugging, ext4 might be used.
    if (Mount(device_file, target, "ext4", mount_flags, nullptr)) {
      LOG(INFO) << "Mounted " << source << " as ext4";
      return true;
    }

    // Mount failed. Remove |source| from the loop device so that |device_num|
    // can be reused.
    if (ioctl(scoped_loop_fd.get(), LOOP_CLR_FD) < 0)
      PLOG(ERROR) << "Failed to remove " << source << " from " << device_file;
    return false;
  }
};

bool AdvanceEnumeratorWithStat(base::FileEnumerator* traversal,
                               base::FilePath* out_next_path,
                               struct stat* out_next_stat) {
  DCHECK(out_next_path);
  DCHECK(out_next_stat);
  *out_next_path = traversal->Next();
  if (out_next_path->empty())
    return false;
  *out_next_stat = traversal->GetInfo().stat();
  return true;
}

}  // namespace

ScopedMount::ScopedMount(const base::FilePath& path, ArcMounter* mounter)
    : mounter_(mounter), path_(path) {}

ScopedMount::~ScopedMount() {
  PLOG_IF(INFO, !mounter_->UmountLazily(path_))
      << "Ignoring failure to umount " << path_.value();
}

// static
std::unique_ptr<ScopedMount> ScopedMount::CreateScopedMount(
    ArcMounter* mounter,
    const std::string& source,
    const base::FilePath& target,
    const char* filesystem_type,
    unsigned long mount_flags,  // NOLINT(runtime/int)
    const char* data) {
  if (!mounter->Mount(source, target, filesystem_type, mount_flags, data))
    return nullptr;
  return std::make_unique<ScopedMount>(target, mounter);
}

// static
std::unique_ptr<ScopedMount> ScopedMount::CreateScopedLoopMount(
    ArcMounter* mounter,
    const std::string& source,
    const base::FilePath& target,
    unsigned long flags) {  // NOLINT(runtime/int)
  if (!mounter->LoopMount(source, target, flags))
    return nullptr;
  return std::make_unique<ScopedMount>(target, mounter);
}

// static
std::unique_ptr<ScopedMount> ScopedMount::CreateScopedBindMount(
    ArcMounter* mounter,
    const base::FilePath& old_path,
    const base::FilePath& new_path) {
  if (!mounter->BindMount(old_path, new_path))
    return nullptr;
  return std::make_unique<ScopedMount>(new_path, mounter);
}

ScopedMountNamespace::ScopedMountNamespace(base::ScopedFD mount_namespace_fd)
    : mount_namespace_fd_(std::move(mount_namespace_fd)) {}

ScopedMountNamespace::~ScopedMountNamespace() {
  PLOG_IF(ERROR, setns(mount_namespace_fd_.get(), CLONE_NEWNS) != 0)
      << "Ignoring failure to restore original mount namespace";
}

// static
std::unique_ptr<ScopedMountNamespace>
ScopedMountNamespace::CreateScopedMountNamespaceForPid(pid_t pid) {
  constexpr char kCurrentMountNamespacePath[] = "/proc/self/ns/mnt";
  base::ScopedFD original_mount_namespace_fd(
      open(kCurrentMountNamespacePath, O_RDONLY));
  if (!original_mount_namespace_fd.is_valid()) {
    PLOG(ERROR) << "Failed to get the original mount namespace FD";
    return nullptr;
  }
  base::ScopedFD mount_namespace_fd(
      open(base::StringPrintf("/proc/%d/ns/mnt", pid).c_str(), O_RDONLY));
  if (!mount_namespace_fd.is_valid()) {
    PLOG(ERROR) << "Failed to get PID " << pid << "'s mount namespace FD";
    return nullptr;
  }

  if (setns(mount_namespace_fd.get(), CLONE_NEWNS) != 0) {
    PLOG(ERROR) << "Failed to enter PID " << pid << "'s mount namespace";
    return nullptr;
  }
  return std::make_unique<ScopedMountNamespace>(
      std::move(original_mount_namespace_fd));
}

std::string GetEnvOrDie(base::Environment* env, const char* name) {
  DCHECK(env);
  std::string result;
  CHECK(env->GetVar(name, &result)) << name << " not found";
  return result;
}

bool GetBooleanEnvOrDie(base::Environment* env, const char* name) {
  return GetEnvOrDie(env, name) == "1";
}

base::FilePath GetFilePathOrDie(base::Environment* env, const char* name) {
  return base::FilePath(GetEnvOrDie(env, name));
}

base::FilePath Realpath(const base::FilePath& path) {
  // We cannot use base::NormalizeFilePath because the function fails
  // if |path| points to a directory (for Windows compatibility.)
  char buf[PATH_MAX] = {};
  if (!realpath(path.value().c_str(), buf)) {
    if (errno != ENOENT)
      PLOG(WARNING) << "Failed to resolve " << path.value();
    return path;
  }
  return base::FilePath(buf);
}

bool MkdirRecursively(const base::FilePath& full_path) {
  if (!full_path.IsAbsolute()) {
    LOG(INFO) << "Relative paths are not supported: " << full_path.value();
    return false;
  }

  // Collect a list of all parent directories.
  std::vector<std::string> components;
  full_path.GetComponents(&components);
  DCHECK(!components.empty());

  base::ScopedFD fd(OpenSafely(base::FilePath("/"), O_RDONLY, 0));
  if (!fd.is_valid())
    return false;

  // Iterate through the parents and create the missing ones. '+ 1' is for
  // skipping "/".
  for (std::vector<std::string>::const_iterator i = components.begin() + 1;
       i != components.end(); ++i) {
    // Try to create the directory. Note that Chromium's MkdirRecursively() uses
    // 0700, but we use 0755.
    if (mkdirat(fd.get(), i->c_str(), 0755) != 0) {
      if (errno != EEXIST) {
        PLOG(ERROR) << "Failed to mkdirat " << *i
                    << ": full_path=" << full_path.value();
        return false;
      }

      // The path already exists. Make sure that the path is a directory.
      struct stat st;
      if (fstatat(fd.get(), i->c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
        PLOG(ERROR) << "Failed to fstatat " << *i
                    << ": full_path=" << full_path.value();
        return false;
      }
      if (!S_ISDIR(st.st_mode)) {
        LOG(ERROR) << *i << " is not a directory: st_mode=" << st.st_mode
                   << ", full_path=" << full_path.value();
        return false;
      }
    }

    // Updates the FD so it refers to the new directory created or checked
    // above.
    const int new_fd =
        openat(fd.get(), i->c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK, 0);
    if (new_fd < 0) {
      PLOG(ERROR) << "Failed to openat " << *i
                  << ": full_path=" << full_path.value();
      return false;
    }
    fd.reset(new_fd);
    continue;
  }
  return true;
}

bool Chown(uid_t uid, gid_t gid, const base::FilePath& path) {
  base::ScopedFD fd(OpenSafely(path, O_RDONLY, 0));
  if (!fd.is_valid())
    return false;
  return fchown(fd.get(), uid, gid) == 0;
}

bool Chcon(const std::string& context, const base::FilePath& path) {
  if (lsetfilecon(path.value().c_str(), context.c_str()) < 0) {
    PLOG(ERROR) << "Could not label " << path.value() << " with " << context;
    return false;
  }

  return true;
}

bool InstallDirectory(mode_t mode,
                      uid_t uid,
                      gid_t gid,
                      const base::FilePath& path) {
  if (!MkdirRecursively(path))
    return false;

  base::ScopedFD fd(OpenSafely(path, O_RDONLY, 0));
  if (!fd.is_valid())
    return false;

  // Unlike 'mkdir -m mode -p' which does not change modes when the path already
  // exists, 'install -d' always sets modes and owner regardless of whether the
  // path exists or not.
  const bool chown_result = (fchown(fd.get(), uid, gid) == 0);
  const bool chmod_result = SetPermissions(fd.get(), mode);
  return chown_result && chmod_result;
}

bool WriteToFile(const base::FilePath& file_path,
                 mode_t mode,
                 const std::string& content) {
  // Use the same mode as base/files/file_posix.cc's.
  constexpr mode_t kMode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

  base::ScopedFD fd(OpenSafely(file_path, O_WRONLY | O_CREAT | O_TRUNC, kMode));
  if (!fd.is_valid())
    return false;
  if (!SetPermissions(fd.get(), mode))
    return false;
  if (content.empty())
    return true;

  // Note: WriteFileDescriptor() makes a best effort to write all data.
  // While-loop for handling partial-write is not needed here.
  return base::WriteFileDescriptor(fd.get(), content.c_str(), content.size());
}

bool GetPropertyFromFile(const base::FilePath& prop_file_path,
                         const std::string& prop_name,
                         std::string* out_prop) {
  const std::string line_prefix_to_find = prop_name + '=';
  if (FindLine(prop_file_path, base::Bind(&FindProperty, line_prefix_to_find),
               out_prop)) {
    return true;  // found the line.
  }
  LOG(WARNING) << prop_name << " is not in " << prop_file_path.value();
  return false;
}

bool GetFingerprintFromPackagesXml(const base::FilePath& packages_xml_path,
                                   std::string* out_fingerprint) {
  if (FindLine(packages_xml_path, base::Bind(&FindFingerprint),
               out_fingerprint)) {
    return true;  // found it.
  }
  LOG(WARNING) << "No fingerprint found in " << packages_xml_path.value();
  return false;
}

bool CreateOrTruncate(const base::FilePath& file_path, mode_t mode) {
  return WriteToFile(file_path, mode, std::string());
}

bool WaitForPaths(std::initializer_list<base::FilePath> paths,
                  const base::TimeDelta& timeout,
                  base::TimeDelta* out_elapsed) {
  const base::TimeDelta sleep_interval = timeout / 20;
  std::list<base::FilePath> left(paths);

  base::ElapsedTimer timer;
  do {
    left.erase(std::remove_if(left.begin(), left.end(), base::PathExists),
               left.end());
    if (left.empty())
      break;  // all paths are found.
    base::PlatformThread::Sleep(sleep_interval);
  } while (timeout >= timer.Elapsed());

  if (out_elapsed)
    *out_elapsed = timer.Elapsed();

  for (const auto& path : left)
    LOG(ERROR) << path.value() << " not found";
  return left.empty();
}

bool LaunchAndWait(const std::vector<std::string>& argv) {
  base::Process process(base::LaunchProcess(argv, base::LaunchOptions()));
  if (!process.IsValid())
    return false;
  int exit_code = -1;
  return process.WaitForExit(&exit_code) && (exit_code == 0);
}

bool RestoreconRecursively(const std::vector<base::FilePath>& directories) {
  return RestoreconInternal(directories, true /* is_recursive */);
}

bool Restorecon(const std::vector<base::FilePath>& paths) {
  return RestoreconInternal(paths, false /* is_recursive */);
}

std::string GenerateFakeSerialNumber(const std::string& chromeos_user,
                                     const std::string& salt) {
  constexpr size_t kMaxHardwareIdLen = 20;
  const std::string hash(crypto::SHA256HashString(chromeos_user + salt));
  return base::HexEncode(hash.data(), hash.length())
      .substr(0, kMaxHardwareIdLen);
}

uint64_t GetArtCompilationOffsetSeed(const std::string& image_build_id,
                                     const std::string& salt) {
  uint64_t result = 0;
  std::string input;
  do {
    input += image_build_id + salt;
    crypto::SHA256HashString(input, &result, sizeof(result));
  } while (!result);
  return result;
}

void MoveDataAppOatDirectory(const base::FilePath& data_app_directory,
                             const base::FilePath& old_executables_directory) {
  base::FileEnumerator dir_enum(data_app_directory, false /* recursive */,
                                base::FileEnumerator::DIRECTORIES);
  for (base::FilePath pkg_directory_name = dir_enum.Next();
       !pkg_directory_name.empty(); pkg_directory_name = dir_enum.Next()) {
    const base::FilePath oat_directory = pkg_directory_name.Append("oat");
    if (!PathExists(oat_directory))
      continue;

    const base::FilePath temp_oat_directory = old_executables_directory.Append(
        "oat-" + pkg_directory_name.BaseName().value());
    base::File::Error file_error;
    if (!base::ReplaceFile(oat_directory, temp_oat_directory, &file_error)) {
      PLOG(ERROR) << "Failed to move cache folder " << oat_directory.value()
                  << ". Error code: " << file_error;
    }
  }
}

bool DeleteFilesInDir(const base::FilePath& directory) {
  base::FileEnumerator files(
      directory, true /* recursive */,
      base::FileEnumerator::FILES | base::FileEnumerator::SHOW_SYM_LINKS);
  bool retval = true;
  for (base::FilePath file = files.Next(); !file.empty(); file = files.Next()) {
    if (!DeleteFile(file, false /*recursive*/)) {
      LOG(ERROR) << "Failed to delete file " << file.value();
      retval = false;
    }
  }
  return retval;
}

std::unique_ptr<ArcMounter> GetDefaultMounter() {
  return std::make_unique<ArcMounterImpl>();
}

bool FindLineForTesting(
    const base::FilePath& file_path,
    const base::Callback<bool(const std::string&, std::string*)>& callback,
    std::string* out_string) {
  return FindLine(file_path, callback, out_string);
}

base::ScopedFD OpenSafelyForTesting(const base::FilePath& path,
                                    int flags,
                                    mode_t mode) {
  return OpenSafely(path, flags, mode);
}

std::string GetChromeOsChannelFromFile(
    const base::FilePath& lsb_release_file_path) {
  constexpr char kChromeOsReleaseTrackProp[] = "CHROMEOS_RELEASE_TRACK";
  const std::set<std::string> kChannels = {
      "beta-channel",    "canary-channel", "dev-channel",
      "dogfood-channel", "stable-channel", "testimage-channel"};
  const std::string kUnknown = "unknown";
  const std::string kChannelSuffix = "-channel";

  // Read the channel property from /etc/lsb-release
  std::string chromeos_channel;
  if (!GetPropertyFromFile(lsb_release_file_path, kChromeOsReleaseTrackProp,
                           &chromeos_channel)) {
    LOG(ERROR) << "Failed to get the ChromeOS channel from "
               << lsb_release_file_path.value();
    return kUnknown;
  }

  if (kChannels.find(chromeos_channel) == kChannels.end()) {
    LOG(WARNING) << "Unknown ChromeOS channel: \"" << chromeos_channel << "\"";
    return kUnknown;
  }
  return chromeos_channel.erase(chromeos_channel.find(kChannelSuffix),
                                kChannelSuffix.size());
}

bool GetOciContainerState(const base::FilePath& path,
                          pid_t* out_container_pid,
                          base::FilePath* out_rootfs) {
  // Read the OCI container state from |path|. Its format is documented in
  // https://github.com/opencontainers/runtime-spec/blob/master/runtime.md#state
  std::string json_str;
  if (!base::ReadFileToString(path, &json_str)) {
    PLOG(ERROR) << "Failed to read json string from " << path.value();
    return false;
  }
  std::string error_msg;
  std::unique_ptr<base::Value> container_state_value =
      base::JSONReader::ReadAndReturnError(
          json_str, base::JSON_PARSE_RFC, nullptr /* error_code_out */,
          &error_msg, nullptr /* error_line_out */,
          nullptr /* error_column_out */);
  if (!container_state_value) {
    LOG(ERROR) << "Failed to parse json: " << error_msg;
    return false;
  }
  const base::DictionaryValue* container_state = nullptr;
  if (!container_state_value->GetAsDictionary(&container_state)) {
    LOG(ERROR) << "Failed to read container state as dictionary";
    return false;
  }

  // Get the container PID and the rootfs path.
  if (!container_state->GetInteger("pid", out_container_pid)) {
    LOG(ERROR) << "Failed to get PID from container state";
    return false;
  }
  const base::DictionaryValue* annotations = nullptr;
  if (!container_state->GetDictionary("annotations", &annotations)) {
    LOG(ERROR) << "Failed to get annotations from container state";
    return false;
  }
  std::string container_root_str;
  if (!annotations->GetStringWithoutPathExpansion(
          "org.chromium.run_oci.container_root", &container_root_str)) {
    LOG(ERROR)
        << "Failed to get org.chromium.run_oci.container_root annotation";
    return false;
  }
  base::FilePath container_root(container_root_str);
  if (!base::ReadSymbolicLink(
          container_root.Append("mountpoints/container-root"), out_rootfs)) {
    PLOG(ERROR) << "Failed to read container root symlink";
    return false;
  }

  return true;
}

bool ExpandPropertyContents(const std::string& content,
                            brillo::CrosConfigInterface* config,
                            std::string* expanded_content) {
  const std::vector<std::string> lines = base::SplitString(
      content, "\n", base::WhitespaceHandling::KEEP_WHITESPACE,
      base::SplitResult::SPLIT_WANT_ALL);

  std::string new_properties;
  for (std::string line : lines) {
    // First expand {property} substitutions in the string.  The insertions
    // may contain substitutions of their own, so we need to repeat until
    // nothing more is found.
    bool inserted;
    do {
      inserted = false;
      size_t match_start = line.find('{');
      size_t prev_match = 0;  // 1 char past the end of the previous {} match.
      std::string expanded;
      // Find all of the {} matches on the line.
      while (match_start != std::string::npos) {
        expanded += line.substr(prev_match, match_start - prev_match);

        size_t match_end = line.find('}', match_start);
        if (match_end == std::string::npos) {
          LOG(ERROR) << "Unmatched { found in line: " << line;
          return false;
        }

        const std::string keyword =
            line.substr(match_start + 1, match_end - match_start - 1);
        std::string replacement;
        if (config->GetString(kCrosConfigPropertiesPath, keyword,
                              &replacement)) {
          expanded += replacement;
          inserted = true;
        } else {
          LOG(ERROR) << "Did not find a value for " << keyword
                     << " while expanding " << line;
          return false;
        }

        prev_match = match_end + 1;
        match_start = line.find('{', match_end);
      }
      if (prev_match != std::string::npos)
        expanded += line.substr(prev_match);
      line = expanded;
    } while (inserted);

    new_properties += TruncateAndroidProperty(line) + "\n";
  }

  *expanded_content = new_properties;
  return true;
}

void SetFingerprintsForPackagesCache(const std::string& content,
                                     const std::string& fingerprint,
                                     std::string* new_content) {
  new_content->clear();

  const std::vector<std::string> lines = base::SplitString(
      content, "\n", base::WhitespaceHandling::KEEP_WHITESPACE,
      base::SplitResult::SPLIT_WANT_NONEMPTY);

  int update_count = 0;
  for (std::string line : lines) {
    if (line.find(kElementVersion) == std::string::npos) {
      *new_content += line + "\n";
      continue;
    }
    size_t pos = line.find(kAttributeFingerprint);
    CHECK_NE(std::string::npos, pos) << line;
    pos += strlen(kAttributeFingerprint);
    const size_t end_pos = line.find("\"", pos);
    CHECK_NE(std::string::npos, end_pos) << line;

    const std::string old_fingerprint = line.substr(pos, end_pos - pos);

    LOG(INFO) << "Updated fingerprint " << old_fingerprint << " -> "
              << fingerprint;
    *new_content += line.substr(0, pos);
    *new_content += fingerprint;
    *new_content += line.substr(end_pos);
    *new_content += "\n";
    ++update_count;
  }
  // Two <version> elements in packages xml
  CHECK_EQ(2, update_count) << content;
}

std::string TruncateAndroidProperty(const std::string& line) {
  // If line looks like key=value, cut value down to the max length of an
  // Android property.  Build fingerprint needs special handling to preserve the
  // trailing dev-keys indicator, but other properties can just be truncated.
  size_t eq_pos = line.find('=');
  if (eq_pos == std::string::npos)
    return line;

  std::string val = line.substr(eq_pos + 1);
  base::TrimWhitespaceASCII(val, base::TRIM_ALL, &val);
  if (val.length() <= kAndroidMaxPropertyLength)
    return line;

  const std::string key = line.substr(0, eq_pos);
  LOG(WARNING) << "Truncating property " << key << " value: " << val;
  if (key == "ro.bootimage.build.fingerprint" &&
      base::EndsWith(val, "/dev-keys", base::CompareCase::SENSITIVE)) {
    // Typical format is brand/product/device/....  We want to remove
    // characters from product and device to get below the length limit.
    // Assume device has the format {product}_cheets.
    std::vector<std::string> fields =
        base::SplitString(val, "/", base::WhitespaceHandling::KEEP_WHITESPACE,
                          base::SplitResult::SPLIT_WANT_ALL);

    int remove_chars = (val.length() - kAndroidMaxPropertyLength + 1) / 2;
    CHECK_GT(fields[1].length(), remove_chars) << fields[1];
    fields[1] = fields[1].substr(0, fields[1].length() - remove_chars);
    fields[2] = fields[1] + "_cheets";
    val = base::JoinString(fields, "/");
  } else {
    val = val.substr(0, kAndroidMaxPropertyLength);
  }

  return key + "=" + val;
}

base::ScopedFD OpenFifoSafely(const base::FilePath& path,
                              int flags,
                              mode_t mode) {
  base::ScopedFD fd(OpenSafelyInternal(path, flags, mode));
  if (!fd.is_valid())
    return base::ScopedFD();

  // Ensure the opened file is a regular file or directory.
  struct stat st;
  if (fstat(fd.get(), &st) < 0) {
    PLOG(ERROR) << "Failed to fstat " << path.value();
    return base::ScopedFD();
  }

  if (!S_ISFIFO(st.st_mode)) {
    LOG(ERROR) << path.value() << " is not a FIFO: " << st.st_mode;
    return base::ScopedFD();
  }

  return fd;
}

bool CopyWithAttributes(const base::FilePath& from_readonly_path,
                        const base::FilePath& to_path) {
  DCHECK(from_readonly_path.IsAbsolute());
  DCHECK(to_path.IsAbsolute());

  struct stat from_stat;
  if (lstat(from_readonly_path.value().c_str(), &from_stat) < 0) {
    PLOG(ERROR) << "Couldn't stat source " << from_readonly_path.value();
    return false;
  }

  base::FileEnumerator traversal(from_readonly_path, true /* recursive */,
                                 base::FileEnumerator::FILES |
                                     base::FileEnumerator::SHOW_SYM_LINKS |
                                     base::FileEnumerator::DIRECTORIES);
  base::FilePath current = from_readonly_path;
  do {
    // current is the source path, including from_path, so append
    // the suffix after from_path to to_path to create the target_path.
    base::FilePath target_path(to_path);
    if (from_readonly_path != current &&
        !from_readonly_path.AppendRelativePath(current, &target_path)) {
      LOG(ERROR) << "Failed to create output path segment for "
                 << current.value() << " and " << target_path.value();
      return false;
    }

    base::ScopedFD dirfd(OpenSafely(target_path.DirName(), O_RDONLY, 0));
    if (!dirfd.is_valid()) {
      LOG(ERROR) << "Failed to open " << target_path.DirName().value();
      return false;
    }

    const std::string target_base_name = target_path.BaseName().value();
    if (S_ISDIR(from_stat.st_mode)) {
      if (mkdirat(dirfd.get(), target_base_name.c_str(), from_stat.st_mode) <
          0) {
        PLOG(ERROR) << "Failed to create " << target_path.value();
        return false;
      }
      if (fchownat(dirfd.get(), target_base_name.c_str(), from_stat.st_uid,
                   from_stat.st_gid, 0 /* flags */) < 0) {
        PLOG(ERROR) << "Failed to set onwers " << target_path.value();
        return false;
      }

      if (fchmodat(dirfd.get(), target_base_name.c_str(), from_stat.st_mode,
                   0 /* flags */) < 0) {
        PLOG(ERROR) << "Failed to set permissions " << target_path.value();
        return false;
      }
    } else if (S_ISREG(from_stat.st_mode)) {
      base::ScopedFD fd_read(open(current.value().c_str(), O_RDONLY));
      if (!fd_read.is_valid()) {
        PLOG(ERROR) << "Failed to open for reading " << current.value();
        return false;
      }
      base::ScopedFD fd_write(OpenSafely(
          target_path, O_WRONLY | O_CREAT | O_TRUNC, from_stat.st_mode));
      if (!fd_write.is_valid()) {
        LOG(ERROR) << "Failed to open for writing " << target_path.value();
        return false;
      }

      char buffer[1024];
      while (true) {
        const ssize_t read_bytes = read(fd_read.get(), buffer, sizeof(buffer));
        if (!read_bytes)
          break;
        if (read_bytes < 0) {
          PLOG(ERROR) << "Failed to read " << current.value();
          return false;
        }
        if (!base::WriteFileDescriptor(fd_write.get(), buffer, read_bytes)) {
          PLOG(ERROR) << "Failed to write " << target_path.value();
          return false;
        }
      }
      if (fchown(fd_write.get(), from_stat.st_uid, from_stat.st_gid) < 0) {
        PLOG(ERROR) << "Failed to set owners for " << target_path.value();
        return false;
      }
      // fchmod is necessary because umask might not be zero.
      if (fchmod(fd_write.get(), from_stat.st_mode) < 0) {
        PLOG(ERROR) << "Failed to set permissions for " << target_path.value();
        return false;
      }
    } else if (S_ISLNK(from_stat.st_mode)) {
      base::FilePath target_link;
      if (!base::ReadSymbolicLink(current, &target_link)) {
        PLOG(ERROR) << "Failed to read symbolic link " << current.value();
        return false;
      }
      if (symlinkat(target_link.value().c_str(), dirfd.get(),
                    target_base_name.c_str()) < 0) {
        PLOG(ERROR) << "Failed to create symbolic link " << target_path.value()
                    << " -> " << target_link.value();
        return false;
      }
      if (fchownat(dirfd.get(), target_base_name.c_str(), from_stat.st_uid,
                   from_stat.st_gid, AT_SYMLINK_NOFOLLOW) < 0) {
        PLOG(ERROR) << "Failed to set link owners for " << target_path.value();
        return false;
      }
    } else {
      if (from_readonly_path == current) {
        LOG(ERROR) << "Unsupported root resource type " << current.value();
        return false;
      }
      // Skip
      LOG(WARNING) << "Skip coping " << current.value()
                   << ". It has unsupported type.";
    }
  } while (AdvanceEnumeratorWithStat(&traversal, &current, &from_stat));

  // Copy selinux attributes for top level element only if it exists.
  char* security_context = nullptr;
  if (lgetfilecon(from_readonly_path.value().c_str(), &security_context) < 0) {
    if (errno != ENODATA) {
      PLOG(ERROR) << "Failed to read security context "
                  << from_readonly_path.value();
      return false;
    }

    LOG(INFO) << "selinux attrbites are not set for "
              << from_readonly_path.value();
    return true;
  }

  base::ScopedFD fd(OpenSafely(to_path, O_RDONLY, 0));
  if (fsetfilecon(fd.get(), security_context) < 0) {
    PLOG(ERROR) << "Failed to set security_context " << to_path.value();
    return false;
  }

  return true;
}

}  // namespace arc
