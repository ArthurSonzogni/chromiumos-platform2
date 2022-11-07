// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/tpm_u2f.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>

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
  buffer->clear();

  if (app_id.size() != U2F_APPID_SIZE ||
      user_secret.size() != U2F_USER_SECRET_SIZE) {
    return SAPI_RC_BAD_PARAMETER;
  }

  u2f_generate_req req{};
  std::copy(app_id.begin(), app_id.end(), req.appId);
  std::copy(user_secret.begin(), user_secret.end(), req.userSecret);
  if (consume) {
    req.flags |= G2F_CONSUME;
  }
  if (up_required) {
    req.flags |= U2F_AUTH_FLAG_TUP;
  }

  if (version == 0) {
    if (auth_time_secret_hash.has_value()) {
      return SAPI_RC_BAD_PARAMETER;
    }
  } else if (version == 1) {
    if (!auth_time_secret_hash.has_value() ||
        auth_time_secret_hash->size() != SHA256_DIGEST_SIZE) {
      return SAPI_RC_BAD_PARAMETER;
    }
    req.flags |= U2F_UV_ENABLED_KH;
    std::copy(auth_time_secret_hash->begin(), auth_time_secret_hash->end(),
              req.authTimeSecretHash);
  } else {
    return SAPI_RC_BAD_PARAMETER;
  }

  buffer->resize(sizeof(req));
  memcpy(buffer->data(), &req, sizeof(req));

  return TPM_RC_SUCCESS;
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
  public_key->clear();
  key_handle->clear();

  if (version == 0) {
    if (buffer.length() != sizeof(u2f_generate_resp)) {
      return SAPI_RC_BAD_SIZE;
    }
    public_key->assign(buffer.cbegin() + offsetof(u2f_generate_resp, pubKey),
                       buffer.cbegin() + offsetof(u2f_generate_resp, pubKey) +
                           sizeof(u2f_generate_resp::pubKey));
    key_handle->assign(buffer.cbegin() + offsetof(u2f_generate_resp, keyHandle),
                       buffer.cbegin() +
                           offsetof(u2f_generate_resp, keyHandle) +
                           sizeof(u2f_generate_resp::keyHandle));
  } else if (version == 1) {
    if (buffer.length() != sizeof(u2f_generate_versioned_resp)) {
      return SAPI_RC_BAD_SIZE;
    }
    public_key->assign(
        buffer.cbegin() + offsetof(u2f_generate_versioned_resp, pubKey),
        buffer.cbegin() + offsetof(u2f_generate_versioned_resp, pubKey) +
            sizeof(u2f_generate_versioned_resp::pubKey));
    key_handle->assign(
        buffer.cbegin() + offsetof(u2f_generate_versioned_resp, keyHandle),
        buffer.cbegin() + offsetof(u2f_generate_versioned_resp, keyHandle) +
            sizeof(u2f_generate_versioned_resp::keyHandle));
  } else {
    return SAPI_RC_BAD_PARAMETER;
  }

  return TPM_RC_SUCCESS;
}

TPM_RC Parse_u2f_sign_t(const std::string& buffer,
                        brillo::Blob* sig_r,
                        brillo::Blob* sig_s) {
  return TPM_RC_FAILURE;
}

}  // namespace trunks
