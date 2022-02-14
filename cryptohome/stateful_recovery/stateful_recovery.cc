// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Provides the implementation of StatefulRecovery.

#include "cryptohome/stateful_recovery/stateful_recovery.h"

#include <unistd.h>

#include <string>

#include <base/files/file_path.h>
#include <base/json/json_writer.h>
#include <base/logging.h>
#include <base/strings/string_util.h>
#include <base/values.h>
#include <brillo/syslog_logging.h>
#include <brillo/cryptohome.h>
#include <policy/device_policy.h>
#include <policy/libpolicy.h>

#include "cryptohome/filesystem_layout.h"
#include "cryptohome/platform.h"

using base::FilePath;

namespace cryptohome {

const char StatefulRecovery::kRecoverSource[] =
    "/mnt/stateful_partition/encrypted";
const char StatefulRecovery::kRecoverDestination[] =
    "/mnt/stateful_partition/decrypted";
const char StatefulRecovery::kRecoverBlockUsage[] =
    "/mnt/stateful_partition/decrypted/block-usage.txt";
const char StatefulRecovery::kRecoverFilesystemDetails[] =
    "/mnt/stateful_partition/decrypted/filesystem-details.txt";
const char StatefulRecovery::kFlagFile[] =
    "/mnt/stateful_partition/decrypt_stateful";
const int kDefaultTimeoutMs = 30000;

StatefulRecovery::StatefulRecovery(
    Platform* platform,
    org::chromium::UserDataAuthInterfaceProxyInterface* userdataauth_proxy,
    policy::PolicyProvider* policy_provider,
    std::string flag_file)
    : requested_(false),
      platform_(platform),
      userdataauth_proxy_(userdataauth_proxy),
      policy_provider_(policy_provider),
      flag_file_(FilePath(flag_file)),
      timeout_ms_(kDefaultTimeoutMs) {}

bool StatefulRecovery::Requested() {
  requested_ = ParseFlagFile();
  return requested_;
}

bool StatefulRecovery::CopyPartitionInfo() {
  struct statvfs vfs;

  if (!platform_->StatVFS(FilePath(kRecoverSource), &vfs))
    return false;

  base::Value dv(base::Value::Type::DICTIONARY);
  dv.SetStringKey("filesystem", FilePath(kRecoverSource).value());
  dv.SetIntKey("blocks-total", vfs.f_blocks);
  dv.SetIntKey("blocks-free", vfs.f_bfree);
  dv.SetIntKey("blocks-avail", vfs.f_bavail);
  dv.SetIntKey("inodes-total", vfs.f_files);
  dv.SetIntKey("inodes-free", vfs.f_ffree);
  dv.SetIntKey("inodes-avail", vfs.f_favail);

  std::string output;
  base::JSONWriter::WriteWithOptions(dv, base::JSONWriter::OPTIONS_PRETTY_PRINT,
                                     &output);

  if (!platform_->WriteStringToFile(FilePath(kRecoverBlockUsage), output))
    return false;

  if (!platform_->ReportFilesystemDetails(FilePath(kRecoverSource),
                                          FilePath(kRecoverFilesystemDetails)))
    return false;

  return true;
}

bool StatefulRecovery::CopyUserContents() {
  int rc;
  FilePath path;

  if (!Mount(user_, passkey_, &path)) {
    // mountfn_ logged the error already.
    return false;
  }

  rc = platform_->Copy(path, FilePath(kRecoverDestination));

  Unmount();
  // If it failed, unmountfn_ would log the error.

  if (rc)
    return true;
  LOG(ERROR) << "Failed to copy " << path.value();
  return false;
}

bool StatefulRecovery::CopyPartitionContents() {
  int rc;

  rc = platform_->Copy(FilePath(kRecoverSource), FilePath(kRecoverDestination));
  if (rc)
    return true;
  LOG(ERROR) << "Failed to copy " << FilePath(kRecoverSource).value();
  return false;
}

bool StatefulRecovery::RecoverV1() {
  // Version 1 requires write protect be disabled.
  if (platform_->FirmwareWriteProtected()) {
    LOG(ERROR) << "Refusing v1 recovery request: firmware is write protected.";
    return false;
  }

  if (!CopyPartitionContents())
    return false;
  if (!CopyPartitionInfo())
    return false;

  return true;
}

bool StatefulRecovery::RecoverV2() {
  bool wrote_data = false;
  bool is_authenticated_owner = false;

  // If possible, copy user contents.
  if (CopyUserContents()) {
    wrote_data = true;
    // If user authenticated, check if they are the owner.
    if (IsOwner(user_)) {
      is_authenticated_owner = true;
    }
  }

  // Version 2 requires either write protect disabled or system owner.
  if (!platform_->FirmwareWriteProtected() || is_authenticated_owner) {
    if (!CopyPartitionContents() || !CopyPartitionInfo()) {
      // Even if we wrote out user data, claim failure here if the
      // encrypted-stateful partition couldn't be extracted.
      return false;
    }
    wrote_data = true;
  }

  return wrote_data;
}

bool StatefulRecovery::Recover() {
  if (!requested_)
    return false;

  // Start with a clean slate. Note that there is a window of opportunity for
  // another process to create the directory with funky permissions after the
  // delete takes place but before we manage to recreate. Since the parent
  // directory is root-owned though, this isn't a problem in practice.
  const FilePath kDestinationPath(kRecoverDestination);
  if (!platform_->DeletePathRecursively(kDestinationPath) ||
      !platform_->CreateDirectory(kDestinationPath)) {
    PLOG(ERROR) << "Failed to create fresh " << kDestinationPath.value();
    return false;
  }

  if (version_ == "2") {
    return RecoverV2();
  } else if (version_ == "1") {
    return RecoverV1();
  } else {
    LOG(ERROR) << "Unknown recovery version: " << version_;
    return false;
  }
}

bool StatefulRecovery::ParseFlagFile() {
  std::string contents;
  size_t delim, pos;
  if (!platform_->ReadFileToString(flag_file_, &contents))
    return false;

  // Make sure there is a trailing newline.
  contents += "\n";

  do {
    pos = 0;
    delim = contents.find("\n", pos);
    if (delim == std::string::npos)
      break;
    version_ = contents.substr(pos, delim);

    if (version_ == "1")
      return true;

    if (version_ != "2")
      break;

    pos = delim + 1;
    delim = contents.find("\n", pos);
    if (delim == std::string::npos)
      break;
    user_ = contents.substr(pos, delim - pos);

    pos = delim + 1;
    delim = contents.find("\n", pos);
    if (delim == std::string::npos)
      break;
    passkey_ = contents.substr(pos, delim - pos);

    return true;
  } while (0);

  // TODO(ellyjones): UMA stat?
  LOG(ERROR) << "Bogus stateful recovery request file:" << contents;
  return false;
}

bool StatefulRecovery::Mount(const std::string& username,
                             const std::string& passkey,
                             FilePath* out_home_path) {
  user_data_auth::MountRequest req;
  req.mutable_account()->set_account_id(username);
  req.mutable_authorization()->mutable_key()->set_secret(passkey);

  user_data_auth::MountReply reply;
  brillo::ErrorPtr error;
  if (!userdataauth_proxy_->Mount(req, &reply, &error, timeout_ms_) || error) {
    LOG(ERROR) << "Mount call failed: " << error->GetMessage();
    return false;
  }
  if (reply.error() !=
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(ERROR) << "Mount during stateful recovery failed: " << reply.error();
    return false;
  }
  LOG(INFO) << "Mount succeeded.";
  const std::string obfuscated_username =
      brillo::cryptohome::home::SanitizeUserName(username);
  *out_home_path = GetUserMountDirectory(obfuscated_username);
  return true;
}

bool StatefulRecovery::Unmount() {
  user_data_auth::UnmountRequest req;

  user_data_auth::UnmountReply reply;
  brillo::ErrorPtr error;
  if (!userdataauth_proxy_->Unmount(req, &reply, &error, timeout_ms_) ||
      error) {
    LOG(ERROR) << "Unmount call failed: " << error->GetMessage();
    return false;
  }
  if (reply.error() !=
      user_data_auth::CryptohomeErrorCode::CRYPTOHOME_ERROR_NOT_SET) {
    LOG(ERROR) << "Unmount failed: " << reply.error();
    printf("Unmount failed.\n");
    return false;
  }
  LOG(INFO) << "Unmount succeeded.";
  return true;
}

bool StatefulRecovery::IsOwner(const std::string& username) {
  std::string owner;
  policy_provider_->Reload();
  if (!policy_provider_->device_policy_is_loaded())
    return false;
  policy_provider_->GetDevicePolicy().GetOwner(&owner);
  if (username.empty() || owner.empty())
    return false;

  return username == owner;
}

}  // namespace cryptohome
