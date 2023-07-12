// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_MOUNT_PASSTHROUGH_MOUNT_PASSTHROUGH_UTIL_H_
#define ARC_MOUNT_PASSTHROUGH_MOUNT_PASSTHROUGH_UTIL_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>

namespace arc {

// Parsed command line flags.
struct CommandLineFlags {
  std::string source;
  std::string dest;
  std::string fuse_umask;
  int32_t fuse_uid = 0;
  int32_t fuse_gid = 0;
  std::string android_app_access_type;
  bool use_default_selinux_context = false;
  int32_t media_provider_uid = 0;
  bool enable_casefold_lookup = false;
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

// Creates the command line args used for invoking `mount-passthrough` via
// `minijail0` including `minijail0` itself.
std::vector<std::string> CreateMinijailCommandLineArgs(
    const CommandLineFlags& flags);

// Performs casefold lookup by making use of
// base::FilePath::CompareEqualIgnoreCase().
// `root` is a path that acts as the root of a case insensitive filesystem.
// `path` is the path to perform casefold lookup.
// The function just returns `path` if `path` references its parent (checked by
// base::FilePath::ReferencesParent()), or it is not a descendant of `root`.
// Otherwise, it returns a path `R` satisfying the following conditions:
// 1) `R` is a descendant of `root`.
// 2) `R` matches `path` in the case insensitive way.
// 3) Let `X` be a prefix of `path` (in terms of components, not letters). If
//    `X` is a path of an existing file, then `X` is also a prefix of `R`.
// 4) Let `X` be a prefix of `R`, and `Y` be a child of `X`. If `Y` is a path of
//    an existing file under `root` and matches a prefix of `path` in the case
//    insensitive way, then there is `Z` such that `Z` is a child of `X`, a path
//    of an existing file under `root`, matches a prefix of `path` in the case
//    insensitive way, and also a prefix of `R`.
// 5) Let `X` be the longest prefix of `R` such that `X` is a path of an
//    existing file. Let `R` = `X` + `Y`. Then `Y` is a suffix of `path`.
// Note that a path that satisfies the above conditions (hence the return value)
// is uniquely determined if no directory under `root` has a pair of entries
// that have the same name in the case insensitive way.
// Otherwise, there may be multiple paths that satisfy the conditions, in which
// case the function is not guaranteed to return consistent results.
base::FilePath CasefoldLookup(const base::FilePath& root,
                              const base::FilePath& path);

}  // namespace arc

#endif  // ARC_MOUNT_PASSTHROUGH_MOUNT_PASSTHROUGH_UTIL_H_
