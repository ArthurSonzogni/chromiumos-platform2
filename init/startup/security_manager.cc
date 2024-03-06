// Copyright 2022 The ChromiumOS Authors
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
#include <crypto/random.h>
#include <libstorage/platform/platform.h>
#include <linux/loadpin.h>
#include <openssl/sha.h>

#include "init/startup/startup_dep_impl.h"

namespace {

constexpr char kSysKernelSecurity[] = "sys/kernel/security";

constexpr char kDevNull[] = "dev/null";
constexpr char kLoadPinVerity[] = "loadpin/dm-verity";
// During CrOS build phases, this file will be produced and baked into the
// rootfs. Specifically during the DLC build flows.
constexpr char kTrustedDlcVerityDigests[] =
    "opt/google/dlc/_trusted_verity_digests";

constexpr char kProcessMgmtPoliciesDir[] =
    "usr/share/cros/startup/process_management_policies";
constexpr char kProcessMgmtPoliciesDirGID[] =
    "usr/share/cros/startup/gid_process_management_policies";
constexpr char kSafeSetIDProcessMgmtPolicies[] = "safesetid";

constexpr char kLsmInodePolicies[] =
    "sys/kernel/security/chromiumos/inode_security_policies";

constexpr char kNoEarlyKeyFile[] = ".no_early_system_key";
constexpr char kSysKeyBackupFile[] = "unencrypted/preserve/system.key";
constexpr int kKeySize = SHA256_DIGEST_LENGTH;

const std::array<const char*, 5> kSymlinkExceptions = {
    "var/cache/echo", "var/cache/vpd", "var/lib/timezone", "var/log", "home",
};
constexpr char kSymlinkExceptionsDir[] =
    "usr/share/cros/startup/symlink_exceptions";
constexpr char kFifoExceptionsDir[] = "usr/share/cros/startup/fifo_exceptions";
constexpr char kVar[] = "var";

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
bool AccumulatePolicyFiles(libstorage::Platform* platform,
                           const base::FilePath& root,
                           const base::FilePath& output_file,
                           const base::FilePath& policy_dir) {
  if (!platform->FileExists(output_file)) {
    // securityfs files are located elsewhere, return.
    return true;
  }

  if (!platform->DirectoryExists(policy_dir)) {
    LOG(WARNING) << "Can't configure process management security. "
                 << policy_dir << " not found.";
    return false;
  }

  std::unique_ptr<libstorage::FileEnumerator> enumerator(
      platform->GetFileEnumerator(policy_dir, false,
                                  base::FileEnumerator::FileType::FILES));
  std::vector<std::string> combined_policy;
  for (base::FilePath file = enumerator->Next(); !file.empty();
       file = enumerator->Next()) {
    std::string file_str;
    DLOG(INFO) << "Loading: " << file.value();
    if (!platform->ReadFileToString(file, &file_str)) {
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

  DLOG(INFO) << "Applying policy to: " << output_file.value();
  if (!platform->WriteStringToFile(output_file, combined_policy_str)) {
    PLOG(ERROR) << output_file << ": Failed to write to file";
    return false;
  }
  return true;
}

// Determine where securityfs files are placed.
// No inputs, checks for which securityfs file paths exist
// and accumulates files for securityfs.
bool ConfigureProcessMgmtSecurity(libstorage::Platform* platform,
                                  const base::FilePath& root) {
  DLOG(INFO) << "ConfigureProcessMgmtSecurity";
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
      policies_dir.Append("gid_allowlist_policy");
  const base::FilePath pmp_gid = root.Append(kProcessMgmtPoliciesDirGID);

  return AccumulatePolicyFiles(platform, root, uid_mgmt_policies, pmpd) &&
         AccumulatePolicyFiles(platform, root, mgmt_policies, pmpd) &&
         AccumulatePolicyFiles(platform, root, gid_mgmt_policies, pmp_gid);
}

bool SetupLoadPinVerityDigests(libstorage::Platform* platform,
                               const base::FilePath& root,
                               StartupDep* startup_dep) {
  const auto loadpin_verity =
      root.Append(kSysKernelSecurity).Append(kLoadPinVerity);
  const auto trusted_dlc_digests = root.Append(kTrustedDlcVerityDigests);
  const auto dev_null = root.Append(kDevNull);
  // Only try loading the trusted dm-verity root digests if:
  //   1. LoadPin dm-verity attribute is supported.
  //   2a. Trusted list of DLC dm-verity root digest file exists.
  //   2b. Otherwise, we must feed LoadPin with an invalid digests file.

  // Open (write) the LoadPin dm-verity attribute file.
  FILE* fd = platform->OpenFile(loadpin_verity, "w");
  if (!fd) {
    // This means LoadPin dm-verity attribute is not supported.
    // No further action is required.
    if (errno == ENOENT) {
      return true;
    }
    // TODO(kimjae): Need to somehow handle this failure, as this still means
    // later a digest can get fed into LoadPin.
    PLOG(ERROR) << "Failed to open LoadPin verity file.";
    return false;
  }

  // Open (read) the trusted digest file in rootfs.
  FILE* digests_fd = platform->OpenFile(trusted_dlc_digests, "r");
  if (!digests_fd) {
    if (errno == ENOENT) {
      PLOG(WARNING) << "Missing trusted DLC verity digests file.";
      // NOTE: Do not return here, so invalid digests get fed into LoadPin.
    } else {
      PLOG(WARNING) << "Failed to open trusted DLC verity digests file.";
      // NOTE: Do not return here, so invalid digests get fed into LoadPin.
    }
    // Any failure in loading/parsing will block subsequent feeds into LoadPin.
    digests_fd = platform->OpenFile(dev_null, "r");
    if (!digests_fd) {
      PLOG(ERROR) << "Failed to open " << dev_null.value() << ".";
      platform->CloseFile(fd);
      return false;
    }
    LOG(WARNING) << "Forcing LoadPin to ingest /dev/null.";
  }

  // Write trusted digests or /dev/null into LoadPin.
  int arg1 = fileno(digests_fd);
  int ret = ioctl(fileno(fd), LOADPIN_IOC_SET_TRUSTED_VERITY_DIGESTS, &arg1);
  if (ret != 0) {
    PLOG(WARNING) << "Unable to setup trusted DLC verity digests";
  }
  // On success or failure:
  // Subsequent `ioctl` on loadpin/dm-verity should fail as the trusted
  // dm-verity root digest list is not empty or invalid digest file descriptor
  // is fed into LoadPin.
  platform->CloseFile(fd);
  platform->CloseFile(digests_fd);
  return ret == 0;
}

bool BlockSymlinkAndFifo(libstorage::Platform* platform,
                         const base::FilePath& root,
                         const std::string& path) {
  base::FilePath base = root.Append(kLsmInodePolicies);
  base::FilePath sym = base.Append("block_symlink");
  base::FilePath fifo = base.Append("block_fifo");
  bool ret = true;
  if (!platform->WriteStringToFile(sym, path)) {
    PLOG(WARNING) << "Failed to write to block_symlink for " << path;
    ret = false;
  }
  if (!platform->WriteStringToFile(fifo, path)) {
    PLOG(WARNING) << "Failed to write to block_fifo for " << path;
    ret = false;
  }
  return ret;
}

// Generates a system key in test images, before the normal mount-encrypted.
// This allows us to soft-clear the TPM in integration tests w/o accidentally
// wiping encstateful after a reboot.
void CreateSystemKey(libstorage::Platform* platform,
                     const base::FilePath& root,
                     const base::FilePath& stateful,
                     StartupDep* startup_dep,
                     std::string* log_content) {
  base::FilePath no_early = stateful.Append(kNoEarlyKeyFile);
  base::FilePath backup = stateful.Append(kSysKeyBackupFile);
  base::FilePath empty;

  if (platform->FileExists(no_early)) {
    log_content->append("Opt not to create a system key in advance.");
    return;
  }

  log_content->append("Checking if a system key already exists in NVRAM...\n");
  std::string output;
  std::vector<std::string> mnt_enc_info = {"info"};
  if (!startup_dep->MountEncrypted(mnt_enc_info, &output)) {
    log_content->append(output);
    log_content->append("\n");
    if (output.find("NVRAM: available.") != std::string::npos) {
      log_content->append("There is already a system key in NVRAM.\n");
      return;
    }
  }

  log_content->append("No system key found in NVRAM. Start creating one.\n");

  // Generates 32-byte random key material and backs it up.
  unsigned char buf[kKeySize];
  crypto::RandBytes(buf, kKeySize);
  const char* buf_ptr = reinterpret_cast<const char*>(&buf);
  if (!platform->WriteArrayToFile(backup, buf_ptr, kKeySize)) {
    log_content->append("Failed to generate or back up system key material.\n");
    return;
  }

  // Persists system key.
  std::vector<std::string> mnt_enc_set = {"set", backup.value()};
  if (!startup_dep->MountEncrypted(mnt_enc_set, &output)) {
    log_content->append(output);
    log_content->append("Successfully created a system key.");
  }
}

bool AllowSymlink(libstorage::Platform* platform,
                  const base::FilePath& root,
                  const std::string& path) {
  base::FilePath sym = root.Append(kLsmInodePolicies).Append("allow_symlink");
  return platform->WriteStringToFile(sym, path);
}

bool AllowFifo(libstorage::Platform* platform,
               const base::FilePath& root,
               const std::string& path) {
  base::FilePath fifo = root.Append(kLsmInodePolicies).Append("allow_fifo");
  return platform->WriteStringToFile(fifo, path);
}

void SymlinkExceptions(libstorage::Platform* platform,
                       const base::FilePath& root) {
  // Generic symlink exceptions.
  for (auto d_it = kSymlinkExceptions.begin(); d_it != kSymlinkExceptions.end();
       d_it++) {
    base::FilePath d = root.Append(*d_it);
    if (!platform->CreateDirectory(d)) {
      PLOG(WARNING) << "mkdir failed for " << d.value();
    }
    if (!platform->SetPermissions(d, 0755)) {
      PLOG(WARNING) << "Failed to set permissions for " << d.value();
    }
    AllowSymlink(platform, root, d.value());
  }
}

// Project-specific exceptions. Projects may add exceptions by
// adding a file under excepts_dir whose contents contains a list
// of paths (one per line) for which an exception should be made.
// File name should use the following format:
// <project-name>-{symlink|fifo}-exceptions.txt
void ExceptionsProjectSpecific(libstorage::Platform* platform,
                               const base::FilePath& root,
                               const base::FilePath& config_dir,
                               bool (*callback)(libstorage::Platform* platform,
                                                const base::FilePath& root,
                                                const std::string& path)) {
  if (platform->DirectoryExists(config_dir)) {
    std::unique_ptr<libstorage::FileEnumerator> iter(
        platform->GetFileEnumerator(config_dir, false,
                                    base::FileEnumerator::FileType::FILES));
    for (base::FilePath path_file = iter->Next(); !path_file.empty();
         path_file = iter->Next()) {
      if (!platform->FileExists(path_file)) {
        continue;
      }
      std::string contents;
      if (!platform->ReadFileToString(path_file, &contents)) {
        PLOG(WARNING) << "Can't open exceptions file " << path_file.value();
        continue;
      }
      std::vector<std::string> files = base::SplitString(
          contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      for (const auto& path : files) {
        if (path.find("#") == 0) {
          continue;
        } else {
          base::FilePath p(path);
          if (!platform->CreateDirectory(p)) {
            PLOG(WARNING) << "mkdir failed for " << path;
          }
          if (!platform->SetPermissions(p, 0755)) {
            PLOG(WARNING) << "Failed to set permissions for " << path;
          }
          callback(platform, root, path);
        }
      }
    }
  }
}

// Set up symlink traversal and FIFO blocking policy, and project
// specific symlink and FIFO exceptions.
void ConfigureFilesystemExceptions(libstorage::Platform* platform,
                                   const base::FilePath& root) {
  // Set up symlink traversal and FIFO blocking policy for /var, which may
  // reside on a separate file system than /mnt/stateful_partition. Block
  // symlink traversal and opening of FIFOs by default, but allow exceptions
  // in the few instances where they are used intentionally.
  BlockSymlinkAndFifo(platform, root, root.Append(kVar).value());
  SymlinkExceptions(platform, root);
  // Project-specific symlink exceptions. Projects may add exceptions by
  // adding a file under /usr/share/cros/startup/symlink_exceptions/ whose
  // contents contains a list of paths (one per line) for which an exception
  // should be made. File name should use the following format:
  // <project-name>-symlink-exceptions.txt
  base::FilePath sym_excepts = root.Append(kSymlinkExceptionsDir);
  ExceptionsProjectSpecific(platform, root, sym_excepts, &AllowSymlink);

  // Project-specific FIFO exceptions. Projects may add exceptions by adding
  // a file under /usr/share/cros/startup/fifo_exceptions/ whose contents
  // contains a list of paths (one per line) for which an exception should be
  // made. File name should use the following format:
  // <project-name>-fifo-exceptions.txt
  base::FilePath fifo_excepts = root.Append(kFifoExceptionsDir);
  ExceptionsProjectSpecific(platform, root, fifo_excepts, &AllowFifo);
}

}  // namespace startup
