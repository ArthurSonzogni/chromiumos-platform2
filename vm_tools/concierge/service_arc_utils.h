// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_SERVICE_ARC_UTILS_H_
#define VM_TOOLS_CONCIERGE_SERVICE_ARC_UTILS_H_

#include <string>

#include <base/files/file_path.h>
#include <vm_concierge/concierge_service.pb.h>

namespace vm_tools::concierge {

// Disk index of the /data disk. It is the 4th disk in request.disks().
constexpr unsigned int kDataDiskIndex = 3;

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

// Returns true if the path is a valid demo image path.
bool IsValidDemoImagePath(const base::FilePath& path);

// Returns true if the path is a valid data image path.
bool IsValidDataImagePath(const base::FilePath& path);

// Returns true if the StartArcVmRequest contains valid ARCVM config values.
bool ValidateStartArcVmRequest(const StartArcVmRequest& request);

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_SERVICE_ARC_UTILS_H_
