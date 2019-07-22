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
#include <openssl/sha.h>
#include <selinux/restorecon.h>
#include <selinux/selinux.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>
#include <list>
#include <set>
#include <utility>

#include <base/bind.h>
#include <base/callback_helpers.h>
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
#include <brillo/file_utils.h>
#include <chromeos-config/libcros_config/cros_config.h>
#include <crypto/sha2.h>

using base::StringPiece;

namespace arc {

namespace {

// The path in the chromeos-config database where Android properties will be
// looked up.
constexpr char kCrosConfigPropertiesPath[] = "/arc/build-properties";

// Android property name used to store the board name.
constexpr char kBoardPropertyPrefix[] = "ro.product.board=";

// Android property name for custom key used for Play Auto Install selection.
constexpr char kOEMKey1PropertyPrefix[] = "ro.oem.key1=";

// Configuration property name of an optional string that contains a comma-
// separated list of regions to include in the OEM key property.
constexpr char kPAIRegionsPropertyName[] = "pai-regions";

// Version element prefix in packages.xml and packages_cache.xml files.
constexpr char kElementVersion[] = "<version ";

// Fingerprint attribute prefix in packages.xml and packages_cache.xml files.
constexpr char kAttributeFingerprint[] = " fingerprint=\"";

// Maximum length of an Android property value.
constexpr int kAndroidMaxPropertyLength = 91;

// Gets the loop device path for a loop device number.
base::FilePath GetLoopDevicePath(int32_t device) {
  return base::FilePath(base::StringPrintf("/dev/loop%d", device));
}

// Immediately removes the loop device from the system.
void RemoveLoopDevice(int control_fd, int32_t device) {
  if (ioctl(control_fd, LOOP_CTL_REMOVE, device) < 0)
    PLOG(ERROR) << "Failed to free /dev/loop" << device;
}

// Disassociates the loop device from any file descriptor.
void DisassociateLoopDevice(int loop_fd,
                            const std::string& source,
                            const base::FilePath& device_path) {
  if (ioctl(loop_fd, LOOP_CLR_FD) < 0) {
    PLOG(ERROR) << "Failed to remove " << source << " from "
                << device_path.value();
  }
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
                  std::string* out_prop,
                  const std::string& line) {
  if (base::StartsWith(line, line_prefix_to_find,
                       base::CompareCase::SENSITIVE)) {
    *out_prop = line.substr(line_prefix_to_find.length());
    return true;
  }
  return false;
}

// Helper function for extracting an attribute value from an XML line.
// Expects |key| to be suffixed with '=\"' (e.g. ' sdkVersion=\"').
StringPiece GetAttributeValue(const StringPiece& line, const StringPiece& key) {
  StringPiece::size_type key_begin_pos = line.find(key);
  if (key_begin_pos == StringPiece::npos)
    return StringPiece();
  StringPiece::size_type value_begin_pos = key_begin_pos + key.length();
  StringPiece::size_type value_end_pos = line.find('"', value_begin_pos);
  if (value_end_pos == StringPiece::npos)
    return StringPiece();
  return line.substr(value_begin_pos, value_end_pos - value_begin_pos);
}

// A callback function for GetFingerprintAndSdkVersionFromPackagesXml.
// This checks if the |line| is like
//    <version sdkVersion="25" databaseVersion="3" fingerprint="..." />
// and store the fingerprint part in |out_fingerprint| and the sdkVersion part
// in |out_sdk_version| if it is. Ignore a line with a volumeUuid attribute
// which means that the line is for an external storage.
// What we need is a fingerprint and a sdk version for an internal storage.
bool FindFingerprintAndSdkVersion(std::string* out_fingerprint,
                                  std::string* out_sdk_version,
                                  const std::string& line) {
  constexpr char kAttributeVolumeUuid[] = " volumeUuid=\"";
  constexpr char kAttributeSdkVersion[] = " sdkVersion=\"";
  constexpr char kAttributeDatabaseVersion[] = " databaseVersion=\"";

  // Parsing an XML this way is not very clean but in this case, it works (and
  // fast.) Android's packages.xml is written in com.android.server.pm.Settings'
  // writeLPr(), and the write function always uses Android's FastXmlSerializer.
  // The serializer does not try to pretty-print the XML, and inserts '\n' only
  // to certain places like endTag.
  StringPiece trimmed = base::TrimWhitespaceASCII(line, base::TRIM_ALL);
  if (!base::StartsWith(trimmed, kElementVersion, base::CompareCase::SENSITIVE))
    return false;  // Not a <version> element. Ignoring.

  if (trimmed.find(kAttributeVolumeUuid) != std::string::npos)
    return false;  // This is for an external storage. Ignoring.

  StringPiece fingerprint = GetAttributeValue(trimmed, kAttributeFingerprint);
  if (fingerprint.empty()) {
    LOG(WARNING) << "<version> doesn't have a valid fingerprint: " << trimmed;
    return false;
  }
  StringPiece sdk_version = GetAttributeValue(trimmed, kAttributeSdkVersion);
  if (sdk_version.empty()) {
    LOG(WARNING) << "<version> doesn't have a valid sdkVersion: " << trimmed;
    return false;
  }
  // Also checks existence of databaseVersion.
  if (GetAttributeValue(trimmed, kAttributeDatabaseVersion).empty()) {
    LOG(WARNING) << "<version> doesn't have a databaseVersion: " << trimmed;
    return false;
  }

  fingerprint.CopyToString(out_fingerprint);
  sdk_version.CopyToString(out_sdk_version);
  return true;
}

// A callback function that parses all lines and put key/value pair into the
// |out_properties|. Returns true in case line cannot be parsed in order to stop
// processing next lines.
bool FindAllProperties(std::map<std::string, std::string>* out_properties,
                       const std::string& line) {
  // Ignore empty lines and comments.
  if (line.empty() || line.at(0) == '#') {
    // Continue reading next lines.
    return false;
  }

  std::string::size_type separator = line.find('=');
  if (separator == std::string::npos) {
    LOG(WARNING) << "Failed to parse: " << line;
    // Stop reading next lines on error.
    return true;
  }

  (*out_properties)[line.substr(0, separator)] = line.substr(separator + 1);
  // Continue reading next lines.
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

  bool UmountIfExists(const base::FilePath& path) override {
    if (umount(Realpath(path).value().c_str()) != 0) {
      // We tolerate nothing mounted on the path (EINVAL) and we tolerate the
      // path not existing (ENOENT)
      if (errno != EINVAL && errno != ENOENT) {
        PLOG(ERROR) << "Mount exists but failed to umount " << path.value();
        return false;
      }
    }
    return true;
  }

  bool LoopUmount(const base::FilePath& path) override {
    if (!LoopUmountInternal(path, /*ignore_missing=*/false)) {
      LOG(ERROR) << "Failed to unmount loop " << path.value();
      return false;
    }
    return true;
  }

  bool LoopUmountIfExists(const base::FilePath& path) override {
    if (!LoopUmountInternal(path, /*ignore_missing=*/true)) {
      LOG(ERROR) << "Mount exists but failed to unmount loop " << path.value();
      return false;
    }
    return true;
  }

 private:
  bool LoopUmountInternal(const base::FilePath& path,
                          const bool ignore_missing) {
    struct stat st;
    if (stat(path.value().c_str(), &st) < 0) {
      if (!ignore_missing || errno != ENOENT) {
        PLOG(ERROR) << "Failed to stat " << path.value();
        return false;
      }
      return true;
    }

    if (major(st.st_dev) != LOOP_MAJOR) {
      if (!ignore_missing) {
        LOG(ERROR) << path.value()
                   << " is not loop-mounted. st_dev=" << st.st_dev;
        return false;
      }
      return true;
    }

    bool autoclear = false;
    const base::FilePath device_path = GetLoopDevicePath(minor(st.st_dev));
    {
      base::ScopedFD scoped_loop_fd(
          open(device_path.value().c_str(), O_RDONLY | O_CLOEXEC));
      if (!scoped_loop_fd.is_valid()) {
        PLOG(ERROR) << "Failed to open " << device_path.value();
        return false;
      }

      struct loop_info64 loop_info;
      if (ioctl(scoped_loop_fd.get(), LOOP_GET_STATUS64, &loop_info) < 0) {
        PLOG(ERROR) << "Failed to get info " << device_path.value();
        return false;
      }
      autoclear = loop_info.lo_flags & LO_FLAGS_AUTOCLEAR;
    }

    if (!Umount(path))
      return false;

    if (!autoclear) {
      base::ScopedFD scoped_loop_fd(
          open(device_path.value().c_str(), O_RDWR | O_CLOEXEC));
      if (!scoped_loop_fd.is_valid()) {
        PLOG(ERROR) << "Failed to open " << device_path.value();
        return false;
      }

      if (ioctl(scoped_loop_fd.get(), LOOP_CLR_FD) < 0) {
        PLOG(ERROR) << "Failed to free " << device_path.value();
        return false;
      }
    }

    return true;
  }

  bool LoopMountInternal(const std::string& source,
                         const base::FilePath& target,
                         unsigned long mount_flags,  // NOLINT(runtime/int)
                         bool* out_retry) {
    constexpr char kLoopControl[] = "/dev/loop-control";

    *out_retry = false;
    base::ScopedFD scoped_control_fd(open(kLoopControl, O_RDONLY));
    if (!scoped_control_fd.is_valid()) {
      PLOG(ERROR) << "Failed to open " << kLoopControl;
      return false;
    }

    const int32_t device_num =
        ioctl(scoped_control_fd.get(), LOOP_CTL_GET_FREE);
    if (device_num < 0) {
      PLOG(ERROR) << "Failed to allocate a loop device";
      return false;
    }

    // Cleanup in case mount fails. This frees |device_num| altogether.
    base::ScopedClosureRunner loop_device_cleanup(
        base::Bind(&RemoveLoopDevice, scoped_control_fd.get(), device_num));

    const base::FilePath device_path = GetLoopDevicePath(device_num);
    base::ScopedFD scoped_loop_fd(open(device_path.value().c_str(), O_RDWR));
    if (!scoped_loop_fd.is_valid() < 0) {
      PLOG(ERROR) << "Failed to open " << device_path.value();
      return false;
    }

    const bool is_readonly_mount = mount_flags & MS_RDONLY;
    base::ScopedFD scoped_source_fd(
        open(source.c_str(), is_readonly_mount ? O_RDONLY : O_RDWR));
    if (!scoped_source_fd.is_valid() < 0) {
      // If the open failed because we tried to open a read only file as RW
      // we fallback to opening it with O_RDONLY
      if (!is_readonly_mount && (errno == EROFS || errno == EACCES)) {
        LOG(WARNING) << source << " is write-protected, using read-only";
        scoped_source_fd.reset(open(source.c_str(), O_RDONLY));
      }
      if (!scoped_source_fd.is_valid() < 0) {
        PLOG(ERROR) << "Failed to open " << source;
        return false;
      }
    }

    if (ioctl(scoped_loop_fd.get(), LOOP_SET_FD, scoped_source_fd.get()) < 0) {
      PLOG(ERROR) << "Failed to associate " << source << " with "
                  << device_path.value();
      // Set |out_retry| to true if LOOP_SET_FD returns EBUSY. The errno
      // indicates that another process has grabbed the same |device_num|
      // before arc-setup does that.
      *out_retry = (errno == EBUSY);
      return false;
    }

    // Set the autoclear flag on the loop device, which will release it when
    // there are no more references to it.
    struct loop_info64 loop_info = {};
    if (ioctl(scoped_loop_fd.get(), LOOP_GET_STATUS64, &loop_info) < 0) {
      PLOG(ERROR) << "Failed to get loop status for " << device_path.value();
      return false;
    }
    loop_info.lo_flags |= LO_FLAGS_AUTOCLEAR;
    if (ioctl(scoped_loop_fd.get(), LOOP_SET_STATUS64, &loop_info) < 0) {
      PLOG(ERROR) << "Failed to set autoclear loop status for "
                  << device_path.value();
      return false;
    }
    // Substitute the removal of the device number by disassociating |source|
    // from the loop device, such that the autoclear flag on |device_num| can
    // automatically remove the loop device.
    loop_device_cleanup.ReplaceClosure(base::Bind(
        &DisassociateLoopDevice, scoped_loop_fd.get(), source, device_path));

    if (Mount(device_path.value(), target, "squashfs", mount_flags, nullptr)) {
      ignore_result(loop_device_cleanup.Release());
      return true;
    }

    // For debugging, ext4 might be used.
    if (Mount(device_path.value(), target, "ext4", mount_flags, nullptr)) {
      LOG(INFO) << "Mounted " << source << " as ext4";
      ignore_result(loop_device_cleanup.Release());
      return true;
    }

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

ScopedMount::ScopedMount(const base::FilePath& path,
                         ArcMounter* mounter,
                         bool is_loop)
    : mounter_(mounter), path_(path), is_loop_(is_loop) {}

ScopedMount::~ScopedMount() {
  if (is_loop_) {
    PLOG_IF(INFO, !mounter_->LoopUmount(path_))
        << "Ignoring failure to umount " << path_.value();
  } else {
    PLOG_IF(INFO, !mounter_->Umount(path_))
        << "Ignoring failure to umount " << path_.value();
  }
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
  return std::make_unique<ScopedMount>(target, mounter, false /*is_loop*/);
}

// static
std::unique_ptr<ScopedMount> ScopedMount::CreateScopedLoopMount(
    ArcMounter* mounter,
    const std::string& source,
    const base::FilePath& target,
    unsigned long flags) {  // NOLINT(runtime/int)
  if (!mounter->LoopMount(source, target, flags))
    return nullptr;
  return std::make_unique<ScopedMount>(target, mounter, true /*is_loop*/);
}

// static
std::unique_ptr<ScopedMount> ScopedMount::CreateScopedBindMount(
    ArcMounter* mounter,
    const base::FilePath& old_path,
    const base::FilePath& new_path) {
  if (!mounter->BindMount(old_path, new_path))
    return nullptr;
  return std::make_unique<ScopedMount>(new_path, mounter, false /*is_loop*/);
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

bool Chown(uid_t uid, gid_t gid, const base::FilePath& path) {
  base::ScopedFD fd(brillo::OpenSafely(path, O_RDONLY, 0));
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
  if (!brillo::MkdirRecursively(path, 0755).is_valid())
    return false;

  base::ScopedFD fd(brillo::OpenSafely(path, O_DIRECTORY | O_RDONLY, 0));
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

  base::ScopedFD fd(
      brillo::OpenSafely(file_path, O_WRONLY | O_CREAT | O_TRUNC, kMode));
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
  if (FindLine(prop_file_path,
               base::Bind(&FindProperty, line_prefix_to_find, out_prop))) {
    return true;  // found the line.
  }
  LOG(WARNING) << prop_name << " is not in " << prop_file_path.value();
  return false;
}

bool GetPropertiesFromFile(const base::FilePath& prop_file_path,
                           std::map<std::string, std::string>* out_properties) {
  if (FindLine(prop_file_path,
               base::Bind(&FindAllProperties, out_properties))) {
    // Failed to parse the file.
    out_properties->clear();
    return false;
  }

  return true;
}

bool GetFingerprintAndSdkVersionFromPackagesXml(
    const base::FilePath& packages_xml_path,
    std::string* out_fingerprint,
    std::string* out_sdk_version) {
  if (FindLine(packages_xml_path,
               base::Bind(&FindFingerprintAndSdkVersion, out_fingerprint,
                          out_sdk_version))) {
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

bool MoveDirIntoDataOldDir(const base::FilePath& dir,
                           const base::FilePath& android_data_old_dir) {
  if (!base::DirectoryExists(dir))
    return true;  // Nothing to do.

  // Create |android_data_old_dir| if it doesn't exist.
  if (!base::DirectoryExists(android_data_old_dir)) {
    if (base::PathExists(android_data_old_dir)) {
      LOG(INFO) << "Deleting a file " << android_data_old_dir.value();
      base::DeleteFile(android_data_old_dir, false);
    }
    base::File::Error error;
    if (!base::CreateDirectoryAndGetError(android_data_old_dir, &error)) {
      PLOG(ERROR) << "Failed to create " << android_data_old_dir.value()
                  << " : " << error;
      return false;
    }
  }

  // Create a randomly-named temp dir in |android_data_old_dir|.
  base::FilePath target_dir_name;
  if (!base::CreateTemporaryDirInDir(
          android_data_old_dir,
          base::StringPrintf("%s_", dir.BaseName().value().c_str()),
          &target_dir_name)) {
    LOG(WARNING) << "Failed to create a temporary directory in "
                 << android_data_old_dir.value();
    return false;
  }
  LOG(INFO) << "Renaming " << dir.value() << " to " << target_dir_name.value();

  // Rename |dir| to the temp dir.
  // Note: Renaming a dir to an existing empty dir works.
  if (!base::Move(dir, target_dir_name)) {
    LOG(WARNING) << "Failed to rename " << dir.value() << " to "
                 << target_dir_name.value();
    return false;
  }

  return true;
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

bool FindLine(const base::FilePath& file_path,
              const base::Callback<bool(const std::string&)>& callback) {
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
    if (callback.Run(line))
      return true;
  } while (!file.eof());

  // |callback| didn't find anything in the file.
  return false;
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

    // Special-case ro.product.board to compute ro.oem.key1 at runtime, as it
    // can depend upon the device region.
    std::string property;
    if (FindProperty(kBoardPropertyPrefix, &property, line)) {
      std::string oem_key_property = ComputeOEMKey(config, property);
      new_properties +=
          std::string(kOEMKey1PropertyPrefix) + oem_key_property + "\n";
    }
  }

  *expanded_content = new_properties;
  return true;
}

std::string ComputeOEMKey(brillo::CrosConfigInterface* config,
                          const std::string& board) {
  std::string regions;
  if (!config->GetString(kCrosConfigPropertiesPath, kPAIRegionsPropertyName,
                         &regions)) {
    // No region list found, just use the board name as before.
    return board;
  }

  std::string region_code;
  if (!base::GetAppOutput({"cros_region_data", "region_code"}, &region_code)) {
    LOG(WARNING) << "Failed to get region code";
    return board;
  }

  // Remove trailing newline.
  region_code.erase(std::remove(region_code.begin(), region_code.end(), '\n'),
                    region_code.end());

  // Allow wildcard configuration to indicate that all regions should be
  // included.
  if (regions.compare("*") == 0 && region_code.length() >= 2)
    return board + "_" + region_code;

  // Check to see if region code is in the list of regions that should be
  // included in the property.
  std::vector<std::string> region_vector =
      base::SplitString(regions, ",", base::WhitespaceHandling::TRIM_WHITESPACE,
                        base::SplitResult::SPLIT_WANT_NONEMPTY);
  for (const auto& region : region_vector) {
    if (region_code.compare(region) == 0)
      return board + "_" + region_code;
  }

  return board;
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

    base::ScopedFD dirfd(
        brillo::OpenSafely(target_path.DirName(), O_DIRECTORY | O_RDONLY, 0));
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
      base::ScopedFD fd_write(brillo::OpenSafely(
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

  base::ScopedFD fd(brillo::OpenSafely(to_path, O_RDONLY, 0));
  if (fsetfilecon(fd.get(), security_context) < 0) {
    PLOG(ERROR) << "Failed to set security_context " << to_path.value();
    return false;
  }

  return true;
}

bool IsProcessAlive(pid_t pid) {
  return kill(pid, 0 /* sig */) == 0;
}

bool GetSha1HashOfFiles(const std::vector<base::FilePath>& files,
                        std::string* out_hash) {
  SHA_CTX sha_context;
  SHA1_Init(&sha_context);
  for (const auto& file : files) {
    std::string file_str;
    if (!base::ReadFileToString(file, &file_str))
      return false;
    SHA1_Update(&sha_context, file_str.data(), file_str.size());
  }
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1_Final(hash, &sha_context);
  out_hash->assign(reinterpret_cast<const char*>(hash), sizeof(hash));
  return true;
}

bool SetXattr(const base::FilePath& path,
              const char* name,
              const std::string& value) {
  base::ScopedFD fd(brillo::OpenSafely(path, O_RDONLY, 0));
  if (!fd.is_valid())
    return false;

  if (fsetxattr(fd.get(), name, value.data(), value.size(), 0 /* flags */) !=
      0) {
    PLOG(ERROR) << "Failed to change xattr " << name << " of " << path.value();
    return false;
  }
  return true;
}

bool ShouldDeleteAndroidData(AndroidSdkVersion system_sdk_version,
                             AndroidSdkVersion data_sdk_version) {
  // Downgraded. (b/80113276)
  if (data_sdk_version > system_sdk_version) {
    LOG(INFO) << "Clearing /data dir because ARC was downgraded from "
              << static_cast<int>(data_sdk_version) << " to "
              << static_cast<int>(system_sdk_version) << ".";
    return true;
  }
  // Upgraded from pre-M to post-P. (b/77591360)
  if (data_sdk_version > AndroidSdkVersion::UNKNOWN &&
      data_sdk_version <= AndroidSdkVersion::ANDROID_M &&
      system_sdk_version >= AndroidSdkVersion::ANDROID_P) {
    LOG(INFO) << "Clearing /data dir because ARC was upgraded from pre-M("
              << static_cast<int>(data_sdk_version) << ") to post-P("
              << static_cast<int>(system_sdk_version) << ").";
    return true;
  }
  return false;
}

}  // namespace arc
