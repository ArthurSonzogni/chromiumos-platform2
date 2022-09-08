// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/profiling/profiling.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_util.h>

extern "C" {
const char* __llvm_profile_get_filename();
void __llvm_profile_set_filename(const char*);
}

namespace hwsec_foundation {

#if ENABLE_PROFILING
namespace {

constexpr char kProcessCommmmandNameFilename[] = "/proc/self/comm";
constexpr char kProfileFileDir[] =
    "/mnt/stateful_partition/unencrypted/profraws";
constexpr char kProfileFileSuffix[] = "-%m-%p.profraw";
constexpr char kDefaultPrefix[] = "UNKNOWN";

std::optional<std::string> GetProcessCommandName() {
  std::string name;
  if (!base::ReadFileToString(base::FilePath(kProcessCommmmandNameFilename),
                              &name)) {
    return {};
  }
  // Remove the characters we are not interested in, e.g., new line character.
  base::TrimWhitespaceASCII(name, base::TRIM_TRAILING, &name);
  return name;
}

}  // namespace

void SetUpProfiling() {
  std::string command_name = GetProcessCommandName().value_or(kDefaultPrefix);
  if (command_name == kDefaultPrefix) {
    LOG(WARNING) << ": Cannot fetch command name; use '" << kDefaultPrefix
                 << "' instead.";
  }

  const char* current_profile_path = __llvm_profile_get_filename();
  if (current_profile_path != nullptr && strlen(current_profile_path) != 0) {
    LOG(WARNING) << __func__ << ": Overriding the current profile path: "
                 << current_profile_path;
  }

  // Build the entire string.
  const base::FilePath profile_file_path =
      base::FilePath(kProfileFileDir)
          .Append(base::FilePath(command_name + kProfileFileSuffix));
  // Set the destination filename for profraws.
  __llvm_profile_set_filename(profile_file_path.value().c_str());
}

#else

void SetUpProfiling() {
  // No-ops.
}

#endif

}  // namespace hwsec_foundation
