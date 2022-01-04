// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/fusebox_helper.h"

#include <base/check.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "cros-disks/fuse_mounter.h"
#include "cros-disks/mount_options.h"
#include "cros-disks/mount_point.h"
#include "cros-disks/platform.h"
#include "cros-disks/quote.h"
#include "cros-disks/sandboxed_process.h"
#include "cros-disks/uri.h"
#include "cros-disks/user.h"

namespace cros_disks {

namespace {

const char kType[] = "fusebox";
const char kHelperTool[] = "/usr/bin/fusebox";
const char kOwnerUserName[] = "fuse-fusebox";
const char kDbusSocketPath[] = "/run/dbus";

}  // namespace

FuseBoxHelper::FuseBoxHelper(const Platform* platform,
                             brillo::ProcessReaper* process_reaper)
    : FUSEMounterHelper(platform,
                        process_reaper,
                        kType,
                        /* nosymfollow= */ true,
                        &sandbox_factory_),
      sandbox_factory_(platform,
                       SandboxedExecutable{base::FilePath(kHelperTool)},
                       ResolveFuseBoxOwnerUser(platform),
                       /* has_network_access= */ false) {}

FuseBoxHelper::~FuseBoxHelper() = default;

OwnerUser FuseBoxHelper::ResolveFuseBoxOwnerUser(
    const Platform* platform) const {
  OwnerUser user;
  PCHECK(platform->GetUserAndGroupId(kOwnerUserName, &user.uid, &user.gid));
  return user;
}

bool FuseBoxHelper::CanMount(const std::string& source,
                             const std::vector<std::string>& params,
                             base::FilePath* suggested_name) const {
  const Uri uri = Uri::Parse(source);
  if (!uri.valid() || uri.scheme() != kType)
    return false;

  if (uri.path().empty()) {
    *suggested_name = base::FilePath(kType);
  } else {
    *suggested_name = base::FilePath(uri.path());
  }

  return true;
}

MountErrorType FuseBoxHelper::ConfigureSandbox(
    const std::string& source,
    const base::FilePath& target_path,
    std::vector<std::string> params,
    SandboxedProcess* sandbox) const {
  const Uri uri = Uri::Parse(source);

  if (!uri.valid() || uri.scheme() != kType) {
    LOG(ERROR) << "Invalid source format " << quote(source);
    return MOUNT_ERROR_INVALID_DEVICE_PATH;
  }

  if (uri.path().empty()) {
    LOG(ERROR) << "Invalid source " << quote(source);
    return MOUNT_ERROR_INVALID_DEVICE_PATH;
  }

  if (!sandbox->BindMount(kDbusSocketPath, kDbusSocketPath, true, false)) {
    LOG(ERROR) << "Cannot bind " << quote(kDbusSocketPath);
    return MOUNT_ERROR_INTERNAL;
  }

  for (const auto& parameter : params)
    sandbox->AddArgument(parameter);

  sandbox->AddArgument("--uid=" + base::NumberToString(kChronosUID));
  sandbox->AddArgument("--gid=" + base::NumberToString(kChronosAccessGID));

  return MOUNT_ERROR_NONE;
}

}  // namespace cros_disks
