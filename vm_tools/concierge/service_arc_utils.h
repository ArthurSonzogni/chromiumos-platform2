// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_SERVICE_ARC_UTILS_H_
#define VM_TOOLS_CONCIERGE_SERVICE_ARC_UTILS_H_

#include <set>
#include <string>
#include <vector>

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

// Prefix for Android command-line system properties.
constexpr char kAndroidBootPrefix[] = "androidboot.";
constexpr size_t kAndroidBootPrefixLen = 12;

// Allowlist of androidboot.* properties on the kernel command line.
// Only properties that are fixed during PropertyInit or referenced explicitly
// by their androidboot.* (i.e., not just ro.boot.*) name should be added to
// this list. Please refer to the 'Property Migration' section of
// go/arcvm-prop-blk-device for context.
const std::set<std::string> kBootPropAllowList = {
    // Properties that are fixed by ExportKernelBootProps during Android
    // PropertyInit in property_service.cpp.
    "androidboot.mode",        // ro.bootmode
    "androidboot.baseband",    // ro.baseband
    "androidboot.bootloader",  // ro.bootloader
    "androidboot.hardware",    // ro.hardware
    "androidboot.revision",    // ro.revision
    // Properties that are referenced by their androidboot.* name explicitly
    // somewhere in Android code; i.e., without using the Android
    // PropertyService (i.e., by using getprop, property_get, etc.).
    "androidboot.android_dt_dir",            // fs_mgr, ueventd
    "androidboot.boot_device",               // fs_mgr
    "androidboot.boot_devices",              // fs_mgr
    "androidboot.dtbo_idx",                  // verified boot
    "androidboot.first_stage_console",       // init
    "androidboot.force_normal_boot",         // init
    "androidboot.init_fatal_panic",          // init
    "androidboot.init_fatal_reboot_target",  // init
    "androidboot.partition_map",             // init
    "androidboot.selinux",                   // init
    "androidboot.slot",                      // verified boot
    "androidboot.slot_suffix",               // fs_mgr, verified boot
    "androidboot.verifiedbootstate",         // verified boot
    "androidboot.veritymode",                // verified boot
};

// Returns "/run/daemon-store/crosvm/<owner_id>".
base::FilePath GetCryptohomePath(const std::string& owner_id);

// Returns path for the ARCVM pstore file under user's cryptohome.
base::FilePath GetPstoreDest(const std::string& owner_id);

// Returns path for the ARCVM vmm swap history file under user's cryptohome.
base::FilePath GetVmmSwapUsageHistoryPath(const std::string& owner_id);

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

// Iterates through ARCVM kernel command line |params| to find androidboot.*
// properties, writes them to |runtime_properties| as ro.boot. properties, and
// removes them from |params|. Skips over non-system property parameters and
// those in |kBootPropAllowList|.
bool RelocateBootProps(std::vector<std::string>* params,
                       std::string* runtime_properties);

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_SERVICE_ARC_UTILS_H_
