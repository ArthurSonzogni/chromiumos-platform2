// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_SERVICE_ARC_UTILS_H_
#define VM_TOOLS_CONCIERGE_SERVICE_ARC_UTILS_H_

#include <string>

#include <base/files/file_path.h>
#include <vm_concierge/concierge_service.pb.h>

namespace vm_tools::concierge {
// Default block size for crosvm disk
constexpr size_t kDefaultBlockSize = 512;

// Disk index of the /data disk. It is the 4th disk in request.disks().
constexpr unsigned int kDataDiskIndex = 3;

// Disk index of the /metadata disk. It is the 5th disk in request.disks().
constexpr unsigned int kMetadataDiskIndex = 4;

// Size of the /metadata disk, used when creating the disk at first boot.
constexpr size_t kMetadataDiskSize = 64 * 1024 * 1024;  // 64 MB

// Disk index of the runtime properties file.
// It is the 6th disk in request.disks().
constexpr unsigned int kPropertiesDiskIndex = 5;

// Maximum number of disks that should be in the StartArcvmRequest
constexpr size_t kMaxArcVmDisks = 6;

// Placeholder disk path value used to indicate that there is no disk.
constexpr char kEmptyDiskPath[] = "/dev/null";

// Expected path of the ARCVM vendor image.
constexpr char kVendorImagePath[] = "/opt/google/vms/android/vendor.raw.img";

// Expected path of the ARCVM apex payload image.
constexpr char kApexPayloadImagePath[] =
    "/opt/google/vms/android/apex/payload.img";

// Returns "/run/daemon-store/crosvm/<owner_id>".
base::FilePath GetCryptohomePath(const std::string& owner_id);

// Returns path for the ARCVM pstore file under user's cryptohome.
base::FilePath GetPstoreDest(const std::string& owner_id);

// Returns path for the ARCVM vmm swap history file under user's cryptohome.
base::FilePath GetVmmSwapUsageHistoryPath(const std::string& owner_id);

// Reads contents from |file| into string at |contents| Returns true if
// successful. Returns false if there was an issue.
bool GetFileContents(const base::FilePath& file, std::string& contents);

// Gets value of property |prop_name| from the string |prop_contents| of a
// system property file and writes it into |prop_value|. Returns true if
// property was found and false otherwise.
bool GetPropertyHelper(const std::string& prop_contents,
                       const std::string& prop_name,
                       std::string* prop_value);

// Reads |prop_file| for an Android property with |prop_name|. If found,
// stores its value in |prop_value| and returns true. Else, returns false
// without changing |prop_value|.
bool GetPropertyFromFile(const base::FilePath& prop_file,
                         const std::string& prop_name,
                         std::string* prop_value);

// Returns true if the path is a valid demo image path.
bool IsValidDemoImagePath(const base::FilePath& path);

// Returns true if the path is a valid data image path.
bool IsValidDataImagePath(const base::FilePath& path);

// Returns true if the path is a valid metadata image path.
bool IsValidMetadataImagePath(const base::FilePath& path);

// Returns true if the path is a valid properties file path.
bool IsValidPropertiesFileDiskPath(const base::FilePath& path);

// Returns true if the StartArcVmRequest contains valid ARCVM config values.
bool ValidateStartArcVmRequest(const StartArcVmRequest& request);

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_SERVICE_ARC_UTILS_H_
