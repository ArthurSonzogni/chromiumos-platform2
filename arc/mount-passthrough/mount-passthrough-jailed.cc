// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Runs mount-passthrough with minijail0 as chronos.
// mount-passthrough-jailed is in the process of being ported from shell
// to C++.

#include <stdlib.h>
#include <unistd.h>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

#include "arc/mount-passthrough/mount-passthrough-util.h"

namespace {
// TODO(satorux): Remove this when the shell script is removed.
constexpr char kShellScriptPath[] = "/usr/bin/mount-passthrough-jailed.sh";
}  // namespace

int main(int argc, char* argv[]) {
  util::CommandLineFlags flags;
  util::ParseCommandLine(argc, argv, &flags);

  std::vector<const char*> script_argv;
  script_argv.resize(11);
  // Explicitly use indices rather than push_back() as the indices are
  // used in the shell script as positional parameters.
  script_argv[0] = kShellScriptPath;
  script_argv[1] = flags.source.c_str();
  script_argv[2] = flags.dest.c_str();
  script_argv[3] = flags.fuse_umask.c_str();
  const std::string fuse_uid = base::NumberToString(flags.fuse_uid);
  script_argv[4] = fuse_uid.c_str();
  const std::string fuse_gid = base::NumberToString(flags.fuse_gid);
  script_argv[5] = fuse_gid.c_str();
  script_argv[6] = flags.android_app_access_type.c_str();
  // Counterintuitively, "0" is true in shell script.
  script_argv[7] = flags.use_default_selinux_context ? "0" : "1";
  script_argv[8] = flags.enter_concierge_namespace ? "0" : "1";
  const std::string max_number_of_open_fds =
      base::NumberToString(flags.max_number_of_open_fds);
  script_argv[9] = max_number_of_open_fds.c_str();
  script_argv[10] = nullptr;  // argv should be terminated with nullptr.

  execv(script_argv[0], const_cast<char* const*>(script_argv.data()));
  PLOG(ERROR) << "execve failed with " << script_argv[0];
  return EXIT_FAILURE;  // execve() failed.
}
