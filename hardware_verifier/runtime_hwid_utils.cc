// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hardware_verifier/runtime_hwid_utils.h"

#include <base/files/file_util.h>
#include <brillo/files/file_util.h>

#include "hardware_verifier/runtime_hwid_generator.h"
#include "hardware_verifier/system/context.h"

namespace hardware_verifier {

bool DeleteRuntimeHWIDFromDevice() {
  const auto runtime_hwid_path =
      Context::Get()->root_dir().Append(kRuntimeHWIDFilePath);
  if (!base::PathExists(runtime_hwid_path)) {
    return true;
  }
  return brillo::DeleteFile(runtime_hwid_path);
}

}  // namespace hardware_verifier
