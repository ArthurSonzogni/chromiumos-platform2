// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/startup/security_manager.h"

#include <fcntl.h>
#include <sys/ioctl.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <linux/loadpin.h>

namespace {

constexpr char kSysKernelSecurity[] = "sys/kernel/security";

constexpr char kDevNull[] = "dev/null";
constexpr char kLoadPinVerity[] = "loadpin/dm-verity";
// During CrOS build phases, this file will be produced and baked into the
// rootfs. Specifically during the DLC build flows.
constexpr char kTrustedDlcVerityDigests[] =
    "opt/google/dlc/_trusted_verity_digests";

// Path to the security fs file for configuring process management security
// policies in the chromiumos LSM (used for kernel version <= 4.4).
// TODO(mortonm): Remove this and the corresponding lines in
// add_process_mgmt_policy when all devices have been updated/backported to
// get the SafeSetID LSM functionality.
constexpr char kProcessMgmtPolicies[] =
    "chromiumos/process_management_policies/add_whitelist_policy";
constexpr char kProcessMgmtPoliciesDir[] =
    "usr/share/cros/startup/process_management_policies";
constexpr char kProcessMgmtPoliciesDirGID[] =
    "usr/share/cros/startup/gid_process_management_policies";
constexpr char kSafeSetIDProcessMgmtPolicies[] = "safesetid";

}  // namespace

namespace startup {

// Project-specific process management policies. Projects may add policies by
// adding a file under usr/share/cros/startup/process_management_policies/
// for UID's and under /usr/share/cros/startup/gid_process_management_policies/
// for GID's, whose contents are one or more lines specifying a parent ID
// and a child UID that the parent can use for the purposes of process
// management. There should be one line for every mapping that is to be put in
// the allow list. Lines in the file should use the following format:
// <UID>:<UID> or <GID>:<GID>
//
// For example, if the 'shill' user needs to use 'dhcp', 'openvpn' and 'ipsec'
// and 'syslog' for process management, the file would look like:
// 20104:224
// 20104:217
// 20104:212
// 20104:202
//
// AccumulatePolicyFiles takes in all the files contained in the policy_dir
// reads their contents, copies and appends them to a file determined by
// output_file.
//
// The parameter gid_policies indicates whether the policies are for GIDs, used
// for selecting the correct file
bool AccumulatePolicyFiles(const base::FilePath& root,
                           const base::FilePath& output_file,
                           const base::FilePath& policy_dir,
                           bool gid) {
  if (!base::PathExists(output_file)) {
    // securityfs files are located elsewhere, return.
    return true;
  }

  if (!base::DirectoryExists(policy_dir)) {
    LOG(WARNING) << "Can't configure process management security. "
                 << policy_dir << " not found.";
    return false;
  }

  const base::FilePath pmp =
      root.Append(kSysKernelSecurity).Append(kProcessMgmtPolicies);
  bool pmp_exists = base::PathExists(pmp);
  base::FileEnumerator enumerator(policy_dir, false,
                                  base::FileEnumerator::FileType::FILES);
  std::vector<std::string> combined_policy;
  for (base::FilePath file = enumerator.Next(); !file.empty();
       file = enumerator.Next()) {
    std::string file_str;
    if (!base::ReadFileToString(file, &file_str)) {
      PLOG(WARNING) << "Can't read policy file " << file;
      continue;
    }
    std::vector<std::string> split_files = base::SplitString(
        file_str, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    split_files.erase(std::remove_if(split_files.begin(), split_files.end(),
                                     [&](const std::string line) {
                                       return base::StartsWith(line, "#");
                                     }),
                      split_files.end());
    combined_policy.push_back(base::JoinString(split_files, "\n"));
  }

  std::string combined_policy_str = base::JoinString(combined_policy, "\n");
  combined_policy_str.append("\n");

  if (pmp_exists) {
    // Don't record GID policies into kProcessMgmtPolicies.
    if (!gid) {
      if (!base::WriteFile(pmp, combined_policy_str)) {
        PLOG(ERROR) << pmp << ": Failed to write file";
      }
    }
  } else {
    if (!base::WriteFile(output_file, combined_policy_str)) {
      PLOG(ERROR) << output_file << ": Failed to write to file";
    }
  }
  return true;
}

// Determine where securityfs files are placed.
// No inputs, checks for which securityfs file paths exist
// and accumulates files for securityfs.
bool ConfigureProcessMgmtSecurity(const base::FilePath& root) {
  // For UID relevant files.

  const base::FilePath policies_dir =
      root.Append(kSysKernelSecurity).Append(kSafeSetIDProcessMgmtPolicies);
  // Path to the securityfs file for configuring process management security
  // policies, for UIDs, in the SafeSetID LSM (used for kernel version >= 5.9).
  const base::FilePath uid_mgmt_policies =
      policies_dir.Append("uid_allowlist_policy");
  // Path to the securityfs file for configuring process management security
  // policies in the SafeSetID LSM (used for kernel version >= 4.14)
  const base::FilePath mgmt_policies = policies_dir.Append("whitelist_policy");
  const base::FilePath pmpd = root.Append(kProcessMgmtPoliciesDir);

  // For GID relevant files.
  const base::FilePath gid_mgmt_policies =
      root.Append(kSafeSetIDProcessMgmtPolicies).Append("gid_allowlist_policy");
  const base::FilePath pmp_gid = root.Append(kProcessMgmtPoliciesDirGID);

  return AccumulatePolicyFiles(root, uid_mgmt_policies, pmpd, false) &&
         AccumulatePolicyFiles(root, mgmt_policies, pmpd, false) &&
         AccumulatePolicyFiles(root, gid_mgmt_policies, pmp_gid, true);
}

bool SetupLoadPinVerityDigests(const base::FilePath& root, Platform* platform) {
  const auto loadpin_verity =
      root.Append(kSysKernelSecurity).Append(kLoadPinVerity);
  const auto trusted_dlc_digests = root.Append(kTrustedDlcVerityDigests);
  const auto dev_null = root.Append(kDevNull);
  // Only try loading the trusted dm-verity root digests if:
  //   1. LoadPin dm-verity attribute is supported.
  //   2a. Trusted list of DLC dm-verity root digest file exists.
  //   2b. Otherwise, we must feed LoadPin with an invalid digests file.

  // Open (write) the LoadPin dm-verity attribute file.
  constexpr auto kWriteFlags = O_WRONLY | O_NOFOLLOW | O_CLOEXEC;
  auto fd = platform->Open(loadpin_verity, kWriteFlags);
  if (!fd.is_valid()) {
    // This means LoadPin dm-verity attribute is not supported.
    // No further action is required.
    if (errno == ENOENT) {
      return true;
    }
    // TODO(kimjae): Need to somehow handle this failure, as this still means
    // later a digest can get fed into LoadPin.
    PLOG(WARNING) << "Failed to open LoadPin verity file.";
    return false;
  }

  // Open (read) the trusted digest file in rootfs.
  constexpr auto kReadFlags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC;
  auto digests_fd = platform->Open(trusted_dlc_digests, kReadFlags);
  if (!digests_fd.is_valid()) {
    if (errno != ENOENT) {
      PLOG(WARNING) << "Failed to open trusted DLC verity digests file.";
      // NOTE: Do not return here, so invalid digests get fed into LoadPin.
    }
    // Any failure in loading/parsing will block subsequent feeds into LoadPin.
    digests_fd = platform->Open(dev_null, kReadFlags);
    if (!digests_fd.is_valid()) {
      PLOG(WARNING) << "Failed to open " << dev_null.value() << ".";
      return false;
    }
  }

  // Write trusted digests or /dev/null into LoadPin.
  int arg1 = digests_fd.get();
  int ret =
      platform->Ioctl(fd.get(), LOADPIN_IOC_SET_TRUSTED_VERITY_DIGESTS, &arg1);
  if (ret != 0) {
    PLOG(WARNING) << "Unable to setup trusted DLC verity digests";
  }
  // On success or failure:
  // Subsequent `ioctl` on loadpin/dm-verity should fail as the trusted
  // dm-verity root digest list is not empty or invalid digest file descriptor
  // is fed into LoadPin.
  return ret == 0;
}

}  // namespace startup
