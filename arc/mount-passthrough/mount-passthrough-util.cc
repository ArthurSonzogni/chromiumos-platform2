// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/flag_helper.h>

#include "arc/mount-passthrough/mount-passthrough-util.h"

namespace util {

void ParseCommandLine(int argc,
                      const char* const* argv,
                      CommandLineFlags* flags) {
  DEFINE_string(source, "", "Source path of FUSE mount (required)");
  DEFINE_string(dest, "", "Target path of FUSE mount (required)");
  DEFINE_string(fuse_umask, "",
                "Umask to set filesystem permissions in FUSE (required)");
  DEFINE_int32(fuse_uid, -1, "UID set as file owner in FUSE (required)");
  DEFINE_int32(fuse_gid, -1, "GID set as file group in FUSE (required)");
  DEFINE_string(android_app_access_type, "full", "Access type of Android apps");
  DEFINE_bool(use_default_selinux_context, false,
              "Use default \"fuse\" SELinux context");
  DEFINE_bool(enter_concierge_namespace, false, "Enter concierge namespace");
  // This is larger than the default value 1024 because this process handles
  // many open files. See b/30236190 for more context.
  DEFINE_int32(max_number_of_open_fds, 8192, "Max number of open fds");

  brillo::FlagHelper::Init(argc, argv, "mount-passthrough-jailed");
  flags->source = FLAGS_source;
  flags->dest = FLAGS_dest;
  flags->fuse_umask = FLAGS_fuse_umask;
  flags->fuse_uid = FLAGS_fuse_uid;
  flags->fuse_gid = FLAGS_fuse_gid;
  flags->android_app_access_type = FLAGS_android_app_access_type;
  flags->use_default_selinux_context = FLAGS_use_default_selinux_context;
  flags->enter_concierge_namespace = FLAGS_enter_concierge_namespace;
  flags->max_number_of_open_fds = FLAGS_max_number_of_open_fds;
}

}  // namespace util
