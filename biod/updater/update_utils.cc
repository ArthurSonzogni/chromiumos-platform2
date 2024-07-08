// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/updater/update_utils.h"

#include <optional>
#include <string>
#include <string_view>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <brillo/vcsid.h>

#include "biod/biod_config.h"

namespace {

constexpr char kUpdateDisableFile[] =
    "/var/lib/bio_fw_updater/.disable_fp_updater";

}  // namespace

namespace biod {
namespace updater {

std::string UpdaterVersion() {
  static_assert(brillo::kVCSID,
                "The updater requires VCSID to function properly.");
  return std::string(*brillo::kVCSID);
}

bool UpdateDisallowed(const BiodSystem& system) {
  // Disable updates when /var/lib/bio_fw_updater/.disable_fp_updater exists
  // and Developer Mode can boot from unsigned kernel (it's a bit stronger check
  // than developer mode only).
  if (!system.OnlyBootSignedKernel() &&
      base::PathExists(base::FilePath(kUpdateDisableFile))) {
    return true;
  }

  return false;
}

}  // namespace updater
}  // namespace biod
