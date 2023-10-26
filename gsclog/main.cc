// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/files/file_path.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>
#include <brillo/userdb_utils.h>
#include <libminijail.h>
#include <scoped_minijail.h>

#include "gsclog/gsclog.h"

using gsclog::GscLog;

namespace {
constexpr char kDefaultLogDirectory[] = "/var/log";
constexpr uid_t kRootUID = 0;
constexpr char kGsclogUser[] = "gsclog";
constexpr char kGsclogGroup[] = "gsclog";
}  // namespace

void InitMinijailSandbox() {
  uid_t gsclog_uid;
  gid_t gsclog_gid;
  CHECK(brillo::userdb::GetUserInfo(kGsclogUser, &gsclog_uid, &gsclog_gid))
      << "Error getting gsclog uid and gid.";
  CHECK_EQ(getuid(), kRootUID) << "gsclogd not initialized as root.";

  ScopedMinijail j(minijail_new());
  minijail_no_new_privs(j.get());
  minijail_change_user(j.get(), kGsclogUser);
  minijail_change_group(j.get(), kGsclogGroup);
  minijail_enter(j.get());

  CHECK_EQ(getuid(), gsclog_uid)
      << "gsclogd was not able to drop user privilege.";
  CHECK_EQ(getgid(), gsclog_gid)
      << "gsclogd was not able to drop group privilege.";
}

int main(int argc, char* argv[]) {
  brillo::InitLog(brillo::kLogToSyslog);
  InitMinijailSandbox();
  DEFINE_string(log_directory, kDefaultLogDirectory,
                "Directory where the output logs should be.");
  brillo::FlagHelper::Init(
      argc, argv, "gsclog concatenates GSC logs for use in debugging.");

  GscLog gsc = GscLog(base::FilePath(FLAGS_log_directory));

  return gsc.Fetch();
}
