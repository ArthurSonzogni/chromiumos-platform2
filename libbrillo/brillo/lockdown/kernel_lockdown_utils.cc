// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/lockdown/kernel_lockdown_utils.h>

#include <optional>
#include <string>
#include <string_view>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

namespace brillo {

namespace {
// Limit the amount of data read to safeguard against corrupted files.
inline constexpr int kMaxSize = 1024;
inline constexpr std::string_view kLockdownDisabled = "none";
inline constexpr std::string_view kLockdownIntegrity = "integrity";
inline constexpr std::string_view kLockdownConfidentiality = "confidentiality";
}  // namespace

std::optional<KernelLockdownMode> GetLockdownMode(
    const base::FilePath& kernel_lockdown) {
  std::string content;
  if (base::ReadFileToStringWithMaxSize(base::FilePath(kernel_lockdown),
                                        &content, kMaxSize)) {
    size_t start = content.find('[');
    size_t end = content.find(']', start);
    if (start != std::string::npos && end != std::string::npos) {
      std::string_view sub{&content[start + 1], end - start - 1};
      if (sub == kLockdownDisabled) {
        return KernelLockdownMode::kDisabled;
      }
      if (sub == kLockdownIntegrity) {
        return KernelLockdownMode::kIntegrity;
      }
      if (sub == kLockdownConfidentiality) {
        return KernelLockdownMode::kConfidentiality;
      }
      LOG(ERROR) << "Invalid lockdown mode: " << sub;
    } else {
      LOG(ERROR) << "Bad kernel lockdown file format: " << content;
    }
  } else {
    PLOG(ERROR) << "Failed to read " << kernel_lockdown;
  }
  return std::nullopt;
}

}  // namespace brillo
