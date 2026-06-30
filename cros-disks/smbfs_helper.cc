// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/smbfs_helper.h"

#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/cryptohome.h>

#include "cros-disks/fuse_mounter.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/sandboxed_process.h"
#include "cros-disks/uri.h"

namespace cros_disks {
namespace {

const char kUserName[] = "fuse-smbfs";
const char kHelperTool[] = "/usr/sbin/smbfs";
const char kType[] = "smbfs";
const char kSeccompPolicyFile[] = "/usr/share/policy/smbfs-seccomp.policy";

const char kMojoIdOptionPrefix[] = "mojo_id=";
const char kDbusSocketPath[] = "/run/dbus";
const char kDaemonStorePath[] = "/run/daemon-store/smbfs";

OwnerUser ResolveSmbFsUser(const Platform* platform) {
  OwnerUser user;
  PCHECK(platform->GetUserAndGroupId(kUserName, &user.uid, &user.gid));
  return user;
}

}  // namespace

SmbfsHelper::SmbfsHelper(const Platform* platform,
                         brillo::ProcessReaper* process_reaper,
                         Metrics* metrics)
    : FUSEMounterHelper(platform,
                        process_reaper,
                        kType,
                        /* nosymfollow= */ true,
                        &sandbox_factory_,
                        metrics),
      sandbox_factory_(platform,
                       SandboxedExecutable{base::FilePath(kHelperTool),
                                           base::FilePath(kSeccompPolicyFile)},
                       ResolveSmbFsUser(platform),
                       /* has_network_access= */ true) {}

SmbfsHelper::~SmbfsHelper() = default;

bool SmbfsHelper::CanMount(const std::string& source,
                           const std::vector<std::string>& params,
                           base::FilePath* suggested_name) const {
  const Uri uri = Uri::Parse(source);
  if (!uri.valid() || uri.scheme() != kType) {
    return false;
  }

  if (uri.path().empty()) {
    *suggested_name = base::FilePath(kType);
  } else {
    *suggested_name = base::FilePath(uri.path());
  }
  return true;
}

MountError SmbfsHelper::ConfigureSandbox(const std::string& source,
                                         const base::FilePath& /*target_path*/,
                                         std::vector<std::string> params,
                                         SandboxedProcess* sandbox) const {
  const Uri uri = Uri::Parse(source);
  if (!uri.valid() || uri.scheme() != kType || uri.path().empty()) {
    LOG(ERROR) << "Invalid source " << quote(source);
    return MountError::kInvalidDevicePath;
  }

  // Bind DBus communication socket and daemon-store into the sandbox.
  if (!sandbox->BindMount(kDbusSocketPath, kDbusSocketPath,
                          /* writable= */ true, /* recursive= */ false)) {
    LOG(ERROR) << "Cannot bind " << quote(kDbusSocketPath);
    return MountError::kInternalError;
  }
  // Extract and validate the account_hash to restrict daemon-store access.
  std::string account_hash;
  if (!GetParamValue(params, "account_hash", &account_hash) ||
      account_hash.empty()) {
    LOG(ERROR) << "Missing or empty account_hash option";
    return MountError::kInvalidMountOptions;
  }

  if (!brillo::cryptohome::home::IsSanitizedUserName(account_hash)) {
    LOG(ERROR) << "Invalid user directory format";
    return MountError::kInvalidMountOptions;
  }

  base::FilePath daemon_store_path =
      base::FilePath(kDaemonStorePath).Append(account_hash);
  if (!platform()->DirectoryExists(daemon_store_path.value())) {
    LOG(ERROR) << "Directory does not exist for user";
    return MountError::kInsufficientPermissions;
  }

  // Bind ONLY the specific user's daemon-store directory into the sandbox.
  if (!sandbox->BindMount(daemon_store_path.value(), daemon_store_path.value(),
                          /* writeable= */ true, /* recursive= */ true)) {
    LOG(ERROR) << "Cannot bind " << quote(daemon_store_path.value());
    return MountError::kInternalError;
  }

  std::string options;
  if (!JoinParamsIntoOptions(
          {"uid=1000", "gid=1001", kMojoIdOptionPrefix + uri.path()},
          &options)) {
    return MountError::kInvalidMountOptions;
  }
  sandbox->AddArgument("-o");
  sandbox->AddArgument(options);

  // Prepend "--" to the "log-level=value" param (if present) and pass it on.
  if (std::string level; GetParamValue(params, "log-level", &level)) {
    sandbox->AddArgument(base::StrCat({"--log-level=", level}));
  }

  return MountError::kSuccess;
}

}  // namespace cros_disks
