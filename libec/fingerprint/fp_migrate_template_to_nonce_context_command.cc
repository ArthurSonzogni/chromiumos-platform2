// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libec/fingerprint/fp_migrate_template_to_nonce_context_command.h"

#include <string>

#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>

namespace ec {

bool FpMigrateTemplateToNonceContextCommand::HexStringToBytes(
    const std::string& hex, size_t max_size, brillo::Blob& out) {
  if (!base::HexStringToBytes(hex, &out)) {
    return false;
  }
  if (out.size() > max_size) {
    out.resize(max_size);
  }
  return true;
}

}  // namespace ec
