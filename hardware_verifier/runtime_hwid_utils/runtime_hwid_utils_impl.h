// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_RUNTIME_HWID_UTILS_RUNTIME_HWID_UTILS_IMPL_H_
#define HARDWARE_VERIFIER_RUNTIME_HWID_UTILS_RUNTIME_HWID_UTILS_IMPL_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/files/file_path.h>
#include <brillo/brillo_export.h>
#include <libcrossystem/crossystem.h>

#include "hardware_verifier/runtime_hwid_utils/runtime_hwid_utils.h"

namespace hardware_verifier {

class BRILLO_EXPORT RuntimeHWIDUtilsImpl : public RuntimeHWIDUtils {
 public:
  RuntimeHWIDUtilsImpl();

  RuntimeHWIDUtilsImpl(const RuntimeHWIDUtilsImpl&) = delete;
  RuntimeHWIDUtilsImpl& operator=(const RuntimeHWIDUtilsImpl&) = delete;

  bool DeleteRuntimeHWIDFromDevice() const override;

  std::optional<std::string> GetRuntimeHWID() const override;

 protected:
  explicit RuntimeHWIDUtilsImpl(
      const base::FilePath& root,
      std::unique_ptr<crossystem::Crossystem> crossystem)
      : root_(root), crossystem_(std::move(crossystem)) {}

 private:
  base::FilePath root_;
  std::unique_ptr<crossystem::Crossystem> crossystem_;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_RUNTIME_HWID_UTILS_RUNTIME_HWID_UTILS_IMPL_H_
