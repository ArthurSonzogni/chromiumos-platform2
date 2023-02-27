// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_MOUNT_PASSTHROUGH_MOUNT_PASSTHROUGH_UTIL_H_
#define ARC_MOUNT_PASSTHROUGH_MOUNT_PASSTHROUGH_UTIL_H_

#include <string>

namespace util {

// Parsed command line flags.
struct CommandLineFlags {
  std::string source;
  std::string dest;
  std::string fuse_umask;
  int32_t fuse_uid = 0;
  int32_t fuse_gid = 0;
  std::string android_app_access_type;
  bool use_default_selinux_context = false;
  bool enter_concierge_namespace = false;
  int32_t max_number_of_open_fds = 0;
};

// Parses the command line, and handles the command line flags.
//
// On error, the process exits as a failure with an error message for the
// first-encountered error.
void ParseCommandLine(int argc,
                      const char* const* argv,
                      CommandLineFlags* flags);

}  // namespace util

#endif  // ARC_MOUNT_PASSTHROUGH_MOUNT_PASSTHROUGH_UTIL_H_
