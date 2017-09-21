// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Provides the implementation of StatefulRecovery.

#include "cryptohome/stateful_recovery.h"

#include <linux/reboot.h>
#include <sys/reboot.h>
#include <unistd.h>

#include <string>

#include <base/files/file_path.h>
#include <base/json/json_writer.h>
#include <base/strings/string_util.h>
#include <base/values.h>

#include "cryptohome/platform.h"
#include "cryptohome/service.h"

using base::FilePath;

namespace cryptohome {

const FilePath::CharType StatefulRecovery::kRecoverSource[] =
  FILE_PATH_LITERAL("/mnt/stateful_partition/encrypted");
const FilePath::CharType StatefulRecovery::kRecoverDestination[] =
  FILE_PATH_LITERAL("/mnt/stateful_partition/decrypted");
const FilePath::CharType StatefulRecovery::kRecoverBlockUsage[] =
  FILE_PATH_LITERAL("/mnt/stateful_partition/decrypted/block-usage.txt");
const FilePath::CharType StatefulRecovery::kRecoverFilesystemDetails[] =
  FILE_PATH_LITERAL("/mnt/stateful_partition/decrypted/filesystem-details.txt");
const FilePath::CharType StatefulRecovery::kFlagFile[] =
  FILE_PATH_LITERAL("/mnt/stateful_partition/decrypt_stateful");

StatefulRecovery::StatefulRecovery(Platform *platform, Service *service)
    : requested_(false), platform_(platform), service_(service) { }
StatefulRecovery::~StatefulRecovery() { }

bool StatefulRecovery::Requested() {
  requested_ = ParseFlagFile();
  return requested_;
}

bool StatefulRecovery::CopyPartitionInfo() {
  struct statvfs vfs;

  if (!platform_->StatVFS(FilePath(kRecoverSource), &vfs))
    return false;

  base::DictionaryValue dv;
  dv.SetString("filesystem", FilePath(kRecoverSource).value());
  dv.SetInteger("blocks-total", vfs.f_blocks);
  dv.SetInteger("blocks-free", vfs.f_bfree);
  dv.SetInteger("blocks-avail", vfs.f_bavail);
  dv.SetInteger("inodes-total", vfs.f_files);
  dv.SetInteger("inodes-free", vfs.f_ffree);
  dv.SetInteger("inodes-avail", vfs.f_favail);

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
  gint error_code;
  gboolean result;
  GError *error = NULL;
  FilePath path;

  if (!service_->Mount(user_.c_str(), passkey_.c_str(), false, false,
                       &error_code, &result, &error) || !result) {
    LOG(ERROR) << "Could not authenticate user '" << user_
               << "' for stateful recovery: "
               << (error ? error->message : "[null]")
               << " (code:" << error_code << ")";
    return false;
  }

  if (!service_->GetMountPointForUser(user_.c_str(), &path)) {
    LOG(ERROR) << "Mount point missing after successful mount call!?";
    return false;
  }

  rc = platform_->Copy(path, FilePath(kRecoverDestination));

  service_->UnmountForUser(user_.c_str(), &result, &error);

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
    if (service_->IsOwner(user_)) {
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
  if (!platform_->DeleteFile(kDestinationPath, true) ||
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

void StatefulRecovery::PerformReboot() {
  // TODO(wad) Replace with a mockable helper.
  if (system("/usr/bin/crossystem recovery_request=1") != 0) {
    LOG(ERROR) << "Failed to set recovery request!";
  }
  platform_->Sync();
  reboot(LINUX_REBOOT_CMD_RESTART);
}

bool StatefulRecovery::ParseFlagFile() {
  std::string contents;
  size_t delim, pos;
  if (!platform_->ReadFileToString(FilePath(kFlagFile), &contents))
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

}  // namespace cryptohome
