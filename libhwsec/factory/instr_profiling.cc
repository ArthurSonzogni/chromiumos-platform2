// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/factory/instr_profiling.h"

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/rand_util.h>
#include <base/strings/string_util.h>

#if ENABLE_PROFILING
extern "C" {
int __llvm_profile_runtime;
void __llvm_profile_set_filename(const char*);
int __llvm_profile_write_file(void);
}
#endif

namespace hwsec::register_profiling {

#if ENABLE_PROFILING
namespace {
constexpr char kProfileFileDir[] =
    "/mnt/stateful_partition/unencrypted/profraws";
constexpr char kProfileFileSuffix[] = "-libhwsec-%m-%p.profraw";
constexpr char kProcessCommandNameFilename[] = "/proc/self/comm";
constexpr char kDefaultPrefix[] = "UNKNOWN";

std::optional<std::string> GetProcessCommandName() {
  std::string name;
  if (!base::ReadFileToString(base::FilePath(kProcessCommandNameFilename),
                              &name)) {
    return {};
  }
  // Remove the characters we are not interested in, e.g., new line character.
  base::TrimWhitespaceASCII(name, base::TRIM_TRAILING, &name);
  return name;
}

std::string ConstructFilename() {
  // Get a random uint64_t. It helps maintaining unique profraw filenames.
  std::string random_int = std::to_string(base::RandUint64());

  // Get current process name.
  std::string process_name = GetProcessCommandName().value_or(kDefaultPrefix);

  // Build the entire string.
  const base::FilePath filename =
      base::FilePath(kProfileFileDir)
          .Append(base::FilePath(process_name + "-" + random_int +
                                 kProfileFileSuffix));

  return filename.value();
}
}  // namespace

void SetUp() {
  __llvm_profile_set_filename(ConstructFilename().c_str());
}

void End() {
  __llvm_profile_write_file();
}

#else

void SetUp() {
  // no-ops
}

void End() {
  // no-ops
}

#endif

}  // namespace hwsec::register_profiling
