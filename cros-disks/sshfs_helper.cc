// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/sshfs_helper.h"

#include <algorithm>

#include <base/base64.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>

#include "cros-disks/fuse_mounter.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/mount_point.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/uri.h"

namespace cros_disks {

namespace {

const char kUserName[] = "fuse-sshfs";
const char kHelperTool[] = "/usr/bin/sshfs";
const char kType[] = "sshfs";

const char kOptionIdentityFile[] = "IdentityFile=";
const char kOptionIdentityBase64[] = "IdentityBase64=";
const char kOptionUserKnownHostsFile[] = "UserKnownHostsFile=";
const char kOptionUserKnownHostsBase64[] = "UserKnownHostsBase64=";
const char kOptionHostName[] = "HostName=";
const char kOptionPort[] = "Port=";

const char* const kEnforcedOptions[] = {
    "KbdInteractiveAuthentication=no",
    "PasswordAuthentication=no",
    "BatchMode=yes",
    "follow_symlinks",
};

const char* const kFilteredOptions[] = {
    kOptionIdentityFile, kOptionUserKnownHostsFile,
};

struct Base64FileMapping {
  const char* base64_option;
  const char* file_option;
  const char* filename;
};

const Base64FileMapping kWrittenFiles[] = {
    {kOptionIdentityBase64, kOptionIdentityFile, "id"},
    {kOptionUserKnownHostsBase64, kOptionUserKnownHostsFile, "known_hosts"},
};

class SshfsMounter : public FUSEMounter {
 public:
  SshfsMounter(const std::string& source_path,
               const std::string& target_path,
               const std::string& filesystem_type,
               const MountOptions& mount_options,
               const Platform* platform,
               brillo::ProcessReaper* process_reaper,
               const std::string& mount_program_path,
               const std::string& mount_user,
               const std::string& seccomp_policy,
               const std::vector<BindPath>& accessible_paths)
      : FUSEMounter(source_path,
                    target_path,
                    filesystem_type,
                    mount_options,
                    platform,
                    process_reaper,
                    mount_program_path,
                    mount_user,
                    seccomp_policy,
                    accessible_paths,
                    true /* permit_network_access */) {}

  // FUSEMounter overrides:
  std::unique_ptr<MountPoint> Mount(const std::string& source_path,
                                    const base::FilePath& target_path,
                                    std::vector<std::string> options,
                                    MountErrorType* error) const override {
    // This mounter will be created and invoked by FUSEMountManager, which
    // expects the source to be a URI.
    Uri uri = Uri::Parse(source_path);
    CHECK(uri.valid());
    return FUSEMounter::Mount(uri.path(), target_path, options, error);
  }
};

}  // namespace

SshfsHelper::SshfsHelper(const Platform* platform,
                         brillo::ProcessReaper* process_reaper)
    : FUSEHelper(kType,
                 platform,
                 process_reaper,
                 base::FilePath(kHelperTool),
                 kUserName) {}

SshfsHelper::~SshfsHelper() = default;

std::unique_ptr<FUSEMounter> SshfsHelper::CreateMounter(
    const base::FilePath& working_dir,
    const Uri& source,
    const base::FilePath& target_path,
    const std::vector<std::string>& options) const {
  uid_t sshfs_uid, files_uid;
  gid_t sshfs_gid, files_gid;
  if (!platform()->GetUserAndGroupId(kUserName, &sshfs_uid, &sshfs_gid) ||
      !platform()->GetUserAndGroupId(kFilesUser, &files_uid, nullptr) ||
      !platform()->GetGroupId(kFilesGroup, &files_gid)) {
    LOG(ERROR) << "Invalid user configuration.";
    return nullptr;
  }

  std::vector<std::string> opts = options;

  // Remove options that we will set ourselves.
  for (const auto& filtered : kFilteredOptions) {
    opts.erase(std::remove_if(opts.begin(), opts.end(),
                              [&filtered](const auto& opt) {
                                return base::StartsWith(
                                    opt, filtered,
                                    base::CompareCase::SENSITIVE);
                              }),
               opts.end());
  }

  if (!PrepareWorkingDirectory(working_dir, sshfs_uid, sshfs_gid, &opts)) {
    return nullptr;
  }

  MountOptions mount_options;
  for (const auto& opt : kEnforcedOptions) {
    mount_options.EnforceOption(opt);
  }
  // We don't whitelist *Base64 versions as we replace them with files.
  mount_options.WhitelistOptionPrefix(kOptionIdentityFile);
  mount_options.WhitelistOptionPrefix(kOptionUserKnownHostsFile);
  mount_options.WhitelistOptionPrefix(kOptionHostName);
  mount_options.WhitelistOptionPrefix(kOptionPort);
  mount_options.Initialize(opts, true, base::IntToString(files_uid),
                           base::IntToString(files_gid));

  return std::make_unique<SshfsMounter>(
      source.path(), target_path.value(), type(), mount_options, platform(),
      process_reaper(), program_path().value(), user(), "",
      std::vector<FUSEMounter::BindPath>());
}

bool SshfsHelper::PrepareWorkingDirectory(
    const base::FilePath& working_dir,
    uid_t uid,
    gid_t gid,
    std::vector<std::string>* options) const {
  for (auto& opt : *options) {
    for (const auto& written : kWrittenFiles) {
      if (base::StartsWith(opt, written.base64_option,
                           base::CompareCase::SENSITIVE)) {
        size_t pos = opt.find('=');
        CHECK_NE(std::string::npos, pos) << "option has no value";

        std::string decoded;
        if (!base::Base64Decode(opt.substr(pos + 1), &decoded)) {
          LOG(ERROR) << "Invalid base64 value in "
                     << quote(written.base64_option);
          return false;
        }

        base::FilePath dst =
            base::FilePath(working_dir).Append(written.filename);
        int size = decoded.size();
        if (platform()->WriteFile(dst.value(), decoded.c_str(), size) != size) {
          PLOG(ERROR) << "Cannot write file " << quote(dst);
          return false;
        }

        if (!platform()->SetPermissions(dst.value(), 0600) ||
            !platform()->SetOwnership(dst.value(), uid, gid)) {
          PLOG(ERROR) << "Cannot change owner of file " << quote(dst);
          return false;
        }
        opt.assign(written.file_option + dst.value());
        break;
      }
    }
  }

  // We retain group ownership on the directory to allow potential cleanup
  // of its contents.
  if (!platform()->SetPermissions(working_dir.value(), 0770) ||
      !platform()->SetOwnership(working_dir.value(), uid, getgid())) {
    LOG(ERROR) << "Cannot set proper ownership of working directory "
               << quote(working_dir);
    return false;
  }
  return true;
}

}  // namespace cros_disks
