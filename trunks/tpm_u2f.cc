// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/tpm_u2f.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

#include <brillo/secure_blob.h>

#include "trunks/cr50_headers/u2f.h"
#include "trunks/error_codes.h"

namespace trunks {

TPM_RC Serialize_u2f_generate_t(
    uint8_t version,
    const brillo::Blob& app_id,
    const brillo::SecureBlob& user_secret,
    bool consume,
    bool up_required,
    const std::optional<brillo::Blob>& auth_time_secret_hash,
    std::string* buffer) {
  return TPM_RC_FAILURE;
}

TPM_RC
Serialize_u2f_sign_t(uint8_t version,
                     const brillo::Blob& app_id,
                     const brillo::SecureBlob& user_secret,
                     const std::optional<brillo::SecureBlob>& auth_time_secret,
                     const std::optional<brillo::Blob>& hash_to_sign,
                     bool check_only,
                     bool consume,
                     bool up_required,
                     const brillo::Blob& key_handle,
                     std::string* buffer) {
  return TPM_RC_FAILURE;
}

TPM_RC Serialize_u2f_attest_t(const brillo::SecureBlob& user_secret,
                              uint8_t format,
                              const brillo::Blob& data,
                              std::string* buffer) {
  return TPM_RC_FAILURE;
}

TPM_RC Parse_u2f_generate_t(const std::string& buffer,
                            uint8_t version,
                            brillo::Blob* public_key,
                            brillo::Blob* key_handle) {
  return TPM_RC_FAILURE;
}

TPM_RC Parse_u2f_sign_t(const std::string& buffer,
                        brillo::Blob* sig_r,
                        brillo::Blob* sig_s) {
  return TPM_RC_FAILURE;
}

}  // namespace trunks
