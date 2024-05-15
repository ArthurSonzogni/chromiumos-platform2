// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/service_arc_utils.h"

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <dbus/vm_concierge/dbus-constants.h>

#include "vm_tools/common/naming.h"

namespace vm_tools::concierge {

namespace {

// /home/root/<hash>/crosvm is bind-mounted to /run/daemon-store/crosvm on
// sign-in.
constexpr char kCryptohomeRoot[] = "/run/daemon-store/crosvm";
constexpr char kPstoreExtension[] = ".pstore";
constexpr char kVmmSwapUsageHistoryExtension[] = ".vmm_swap_history";

}  // namespace

base::FilePath GetCryptohomePath(const std::string& owner_id) {
  return base::FilePath(kCryptohomeRoot).Append(owner_id);
}

base::FilePath GetPstoreDest(const std::string& owner_id) {
  return GetCryptohomePath(owner_id)
      .Append(vm_tools::GetEncodedName(kArcVmName))
      .AddExtension(kPstoreExtension);
}

base::FilePath GetVmmSwapUsageHistoryPath(const std::string& owner_id) {
  return GetCryptohomePath(owner_id)
      .Append(kArcVmName)
      .AddExtension(kVmmSwapUsageHistoryExtension);
}

bool GetFileContents(const base::FilePath& file, std::string& contents) {
  if (!base::ReadFileToString(file, &contents)) {
    PLOG(ERROR) << "Failed to read " << file;
    return false;
  }
  return true;
}

bool GetPropertyHelper(const std::string& prop_contents,
                       const std::string& prop_name,
                       std::string* prop_value) {
  const std::string prefix = prop_name + '=';
  const std::vector<std::string> properties = base::SplitString(
      prop_contents, "\n", base::WhitespaceHandling::TRIM_WHITESPACE,
      base::SplitResult::SPLIT_WANT_NONEMPTY);

  // Start search from the end of file because the last value is what
  // gets assigned
  const auto it = std::find_if(
      properties.rbegin(), properties.rend(), [&](const std::string& str) {
        return base::StartsWith(str, prefix, base::CompareCase::SENSITIVE);
      });
  if (it == properties.rend()) {
    return false;
  }

  *prop_value = it->substr(prefix.length());
  return true;
}

bool GetPropertyFromFile(const base::FilePath& prop_file,
                         const std::string& prop_name,
                         std::string* prop_value) {
  std::string contents;
  if (!GetFileContents(prop_file, contents))
    return false;

  if (!GetPropertyHelper(contents, prop_name, prop_value))
    return false;

  return true;
}

bool IsValidDemoImagePath(const base::FilePath& path) {
  // A valid demo image path looks like:
  //   /run/imageloader/demo-mode-resources/<version>/android_demo_apps.squash
  //   <version> part looks like 0.12.34.56 ("[0-9]+(.[0-9]+){0,3}" in regex).
  const std::vector<std::string> c = path.GetComponents();
  return c.size() == 6 && c[0] == "/" && c[1] == "run" &&
         c[2] == "imageloader" && c[3] == "demo-mode-resources" &&
         base::ContainsOnlyChars(c[4], "0123456789.") &&
         !base::StartsWith(c[4], ".") && c[5] == "android_demo_apps.squash";
}

bool IsValidDataImagePath(const base::FilePath& path) {
  const std::vector<std::string> c = path.GetComponents();
  // A disk image created by concierge:
  // /run/daemon-store/crosvm/<hash>/YXJjdm0=.img
  if (c.size() == 6 && c[0] == "/" && c[1] == "run" && c[2] == "daemon-store" &&
      c[3] == "crosvm" && base::ContainsOnlyChars(c[4], "0123456789abcdef") &&
      c[5] == vm_tools::GetEncodedName(kArcVmName) + ".img")
    return true;
  // An LVM block device:
  // /dev/mapper/vm/dmcrypt-<hash>-arcvm
  if (c.size() == 5 && c[0] == "/" && c[1] == "dev" && c[2] == "mapper" &&
      c[3] == "vm" && base::StartsWith(c[4], "dmcrypt-") &&
      base::EndsWith(c[4], "-arcvm"))
    return true;
  return false;
}

bool IsValidMetadataImagePath(const base::FilePath& path) {
  // A valid metadata image path looks like:
  //   /run/daemon-store/crosvm/<hash>/YXJjdm0=.metadata.img
  const std::vector<std::string> c = path.GetComponents();
  return c.size() == 6 && c[0] == "/" && c[1] == "run" &&
         c[2] == "daemon-store" && c[3] == "crosvm" &&
         base::ContainsOnlyChars(c[4], "0123456789abcdef") &&
         c[5] == vm_tools::GetEncodedName(kArcVmName) + ".metadata.img";
}

bool ValidateStartArcVmRequest(const StartArcVmRequest& request) {
  const auto& disks = request.disks();
  if (disks.size() < 1 || disks.size() > 5) {
    LOG(ERROR) << "Invalid number of disks: " << disks.size();
    return false;
  }
  // Disk #0 must be /opt/google/vms/android/vendor.raw.img.
  if (disks[0].path() != kVendorImagePath) {
    LOG(ERROR) << "Disk #0 has invalid path: " << disks[0].path();
    return false;
  }
  // Disk #1 must be a valid demo image path or /dev/null.
  if (disks.size() >= 2 &&
      !IsValidDemoImagePath(base::FilePath(disks[1].path())) &&
      disks[1].path() != kEmptyDiskPath) {
    LOG(ERROR) << "Disk #1 has invalid path: " << disks[1].path();
    return false;
  }
  // Disk #2 must be /opt/google/vms/android/apex/payload.img or /dev/null.
  if (disks.size() >= 3 && disks[2].path() != kApexPayloadImagePath &&
      disks[2].path() != kEmptyDiskPath) {
    LOG(ERROR) << "Disk #2 has invalid path: " << disks[2].path();
    return false;
  }
  // Disk #3 must be a valid data image path or /dev/null.
  if (disks.size() >= kDataDiskIndex + 1) {
    const std::string& disk_path = disks[kDataDiskIndex].path();
    if (!IsValidDataImagePath(base::FilePath(disk_path)) &&
        disk_path != kEmptyDiskPath) {
      LOG(ERROR) << "Disk #3 has invalid path: " << disk_path;
      return false;
    }
    LOG(INFO) << "Android /data disk path: " << disk_path;
  }
  // Disk #4 must be a valid metadata image path or /dev/null.
  if (disks.size() >= kMetadataDiskIndex + 1) {
    const std::string& disk_path = disks[kMetadataDiskIndex].path();
    if (!IsValidMetadataImagePath(base::FilePath(disk_path)) &&
        disk_path != kEmptyDiskPath) {
      LOG(ERROR) << "Disk #4 has invalid path: " << disk_path;
      return false;
    }
    LOG(INFO) << "Android /metadata disk path: " << disk_path;
  }
  return true;
}

}  // namespace vm_tools::concierge
