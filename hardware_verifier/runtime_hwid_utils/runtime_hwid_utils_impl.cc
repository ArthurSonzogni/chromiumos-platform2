// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/runtime_hwid_utils/runtime_hwid_utils_impl.h"

#include <memory>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <brillo/files/file_util.h>
#include <libcrossystem/crossystem.h>

namespace hardware_verifier {

namespace {

constexpr char kRuntimeHWIDFilePath[] =
    "var/cache/hardware_verifier/runtime_hwid";

}  // namespace

RuntimeHWIDUtilsImpl::RuntimeHWIDUtilsImpl()
    : root_("/"), crossystem_(std::make_unique<crossystem::Crossystem>()) {}

bool RuntimeHWIDUtilsImpl::DeleteRuntimeHWIDFromDevice() const {
  const auto runtime_hwid_path = root_.Append(kRuntimeHWIDFilePath);
  if (!base::PathExists(runtime_hwid_path)) {
    return true;
  }
  return brillo::DeleteFile(runtime_hwid_path);
}

}  // namespace hardware_verifier
