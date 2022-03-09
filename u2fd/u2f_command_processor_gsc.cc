// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/u2f_command_processor_gsc.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <base/optional.h>
#include <base/time/time.h>
#include <brillo/dbus/dbus_method_response.h>
#include <chromeos/cbor/values.h>
#include <chromeos/cbor/writer.h>
#include <openssl/sha.h>
#include <trunks/cr50_headers/u2f.h>
#include <u2f/proto_bindings/u2f_interface.pb.h>

#include "u2fd/tpm_vendor_cmd.h"
#include "u2fd/user_state.h"
#include "u2fd/util.h"
#include "u2fd/webauthn_handler.h"

namespace u2f {

namespace {

// COSE key parameters.
// https://tools.ietf.org/html/rfc8152#section-7.1
const int kCoseKeyKtyLabel = 1;
const int kCoseKeyKtyEC2 = 2;
const int kCoseKeyAlgLabel = 3;
const int kCoseKeyAlgES256 = -7;

// Double coordinate curve parameters.
// https://tools.ietf.org/html/rfc8152#section-13.1.1
const int kCoseECKeyCrvLabel = -1;
const int kCoseECKeyXLabel = -2;
const int kCoseECKeyYLabel = -3;

constexpr base::TimeDelta kVerificationTimeout = base::Seconds(10);

// Cr50 Response codes.
// TODO(louiscollard): Don't duplicate these.
constexpr uint32_t kCr50StatusNotAllowed = 0x507;

}  // namespace

U2fCommandProcessorGsc::U2fCommandProcessorGsc(
    TpmVendorCommandProxy* tpm_proxy, std::function<void()> request_presence)
    : tpm_proxy_(tpm_proxy), request_presence_(request_presence) {}

MakeCredentialResponse::MakeCredentialStatus
U2fCommandProcessorGsc::U2fGenerate(
    const std::vector<uint8_t>& rp_id_hash,
    const std::vector<uint8_t>& credential_secret,
    PresenceRequirement presence_requirement,
    bool uv_compatible,
    const brillo::Blob* auth_time_secret_hash,
    std::vector<uint8_t>* credential_id,
    std::vector<uint8_t>* credential_public_key,
    std::vector<uint8_t>* /*unused*/) {
  DCHECK(rp_id_hash.size() == SHA256_DIGEST_LENGTH);

  struct u2f_generate_req generate_req = {};
  if (!util::VectorToObject(rp_id_hash, generate_req.appId,
                            sizeof(generate_req.appId))) {
    return MakeCredentialResponse::INVALID_REQUEST;
  }
  if (!util::VectorToObject(credential_secret, generate_req.userSecret,
                            sizeof(generate_req.userSecret))) {
    return MakeCredentialResponse::INVALID_REQUEST;
  }

  if (uv_compatible) {
    if (!auth_time_secret_hash) {
      LOG(ERROR) << "No auth-time secret hash to use for u2f_generate.";
      return MakeCredentialResponse::INTERNAL_ERROR;
    }
    generate_req.flags |= U2F_UV_ENABLED_KH;
    memcpy(generate_req.authTimeSecretHash, auth_time_secret_hash->data(),
           auth_time_secret_hash->size());
    struct u2f_generate_versioned_resp generate_resp = {};

    MakeCredentialResponse::MakeCredentialStatus status;
    if (presence_requirement != PresenceRequirement::kPowerButton) {
      uint32_t generate_status =
          tpm_proxy_->SendU2fGenerate(generate_req, &generate_resp);
      if (generate_status != 0)
        return MakeCredentialResponse::INTERNAL_ERROR;

      std::vector<uint8_t> public_key;
      util::AppendToVector(generate_resp.pubKey, &public_key);

      util::AppendToVector(EncodeCredentialPublicKeyInCBOR(public_key),
                           credential_public_key);
      util::AppendToVector(generate_resp.keyHandle, credential_id);
      status = MakeCredentialResponse::SUCCESS;
    } else {
      // Require user presence, consume.
      generate_req.flags |= U2F_AUTH_ENFORCE;
      status = SendU2fGenerateWaitForPresence(
          &generate_req, &generate_resp, credential_id, credential_public_key);
    }
    if (status == MakeCredentialResponse::SUCCESS) {
      InsertAuthTimeSecretHashToCredentialId(auth_time_secret_hash,
                                             credential_id);
    }
    return status;
  } else {
    // Non-versioned KH must be signed with power button press.
    if (presence_requirement != PresenceRequirement::kPowerButton)
      return MakeCredentialResponse::INTERNAL_ERROR;
    // Require user presence, consume.
    generate_req.flags |= U2F_AUTH_ENFORCE;
    struct u2f_generate_resp generate_resp = {};
    return SendU2fGenerateWaitForPresence(&generate_req, &generate_resp,
                                          credential_id, credential_public_key);
  }
}

GetAssertionResponse::GetAssertionStatus U2fCommandProcessorGsc::U2fSign(
    const std::vector<uint8_t>& rp_id_hash,
    const std::vector<uint8_t>& hash_to_sign,
    const std::vector<uint8_t>& credential_id,
    const std::vector<uint8_t>& credential_secret,
    const std::vector<uint8_t>* /*unused*/,
    PresenceRequirement presence_requirement,
    std::vector<uint8_t>* signature) {
  DCHECK(rp_id_hash.size() == SHA256_DIGEST_LENGTH);

  if (credential_id.size() == U2F_V1_KH_SIZE + SHA256_DIGEST_LENGTH) {
    // Allow waiving presence if sign_req.authTimeSecret is correct.
    struct u2f_sign_versioned_req sign_req = {};
    if (!util::VectorToObject(rp_id_hash, sign_req.appId,
                              sizeof(sign_req.appId))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_secret, sign_req.userSecret,
                              sizeof(sign_req.userSecret))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    std::vector<uint8_t> key_handle(credential_id);
    RemoveAuthTimeSecretHashFromCredentialId(&key_handle);
    if (!util::VectorToObject(key_handle, &sign_req.keyHandle,
                              sizeof(sign_req.keyHandle))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(hash_to_sign, sign_req.hash,
                              sizeof(sign_req.hash))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    struct u2f_sign_resp sign_resp = {};

    if (presence_requirement != PresenceRequirement::kPowerButton) {
      uint32_t sign_status = tpm_proxy_->SendU2fSign(sign_req, &sign_resp);
      if (sign_status != 0)
        return GetAssertionResponse::INTERNAL_ERROR;

      base::Optional<std::vector<uint8_t>> opt_signature =
          util::SignatureToDerBytes(sign_resp.sig_r, sign_resp.sig_s);
      if (!opt_signature.has_value()) {
        return GetAssertionResponse::INTERNAL_ERROR;
      }
      *signature = *opt_signature;
      return GetAssertionResponse::SUCCESS;
    }

    // Require user presence, consume.
    sign_req.flags |= U2F_AUTH_ENFORCE;
    return SendU2fSignWaitForPresence(&sign_req, &sign_resp, signature);
  } else if (credential_id.size() == U2F_V0_KH_SIZE) {
    // Non-versioned KH must be signed with power button press.
    if (presence_requirement != PresenceRequirement::kPowerButton)
      return GetAssertionResponse::INTERNAL_ERROR;

    struct u2f_sign_req sign_req = {
        .flags = U2F_AUTH_ENFORCE  // Require user presence, consume.
    };
    if (!util::VectorToObject(rp_id_hash, sign_req.appId,
                              sizeof(sign_req.appId))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_secret, sign_req.userSecret,
                              sizeof(sign_req.userSecret))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_id, &sign_req.keyHandle,
                              sizeof(sign_req.keyHandle))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(hash_to_sign, sign_req.hash,
                              sizeof(sign_req.hash))) {
      return GetAssertionResponse::INVALID_REQUEST;
    }

    struct u2f_sign_resp sign_resp = {};
    return SendU2fSignWaitForPresence(&sign_req, &sign_resp, signature);
  } else {
    return GetAssertionResponse::UNKNOWN_CREDENTIAL_ID;
  }
}

HasCredentialsResponse::HasCredentialsStatus
U2fCommandProcessorGsc::U2fSignCheckOnly(
    const std::vector<uint8_t>& rp_id_hash,
    const std::vector<uint8_t>& credential_id,
    const std::vector<uint8_t>& credential_secret,
    const std::vector<uint8_t>* /*unused*/) {
  uint32_t sign_status;

  if (credential_id.size() == U2F_V1_KH_SIZE + SHA256_DIGEST_LENGTH) {
    struct u2f_sign_versioned_req sign_req = {.flags = U2F_AUTH_CHECK_ONLY};
    if (!util::VectorToObject(rp_id_hash, sign_req.appId,
                              sizeof(sign_req.appId))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_secret, sign_req.userSecret,
                              sizeof(sign_req.userSecret))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }
    std::vector<uint8_t> key_handle(credential_id);
    RemoveAuthTimeSecretHashFromCredentialId(&key_handle);
    if (!util::VectorToObject(key_handle, &sign_req.keyHandle,
                              sizeof(sign_req.keyHandle))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }

    struct u2f_sign_resp sign_resp;

    sign_status = tpm_proxy_->SendU2fSign(sign_req, &sign_resp);
    brillo::SecureClearContainer(sign_req.userSecret);
  } else if (credential_id.size() == U2F_V0_KH_SIZE) {
    struct u2f_sign_req sign_req = {.flags = U2F_AUTH_CHECK_ONLY};
    if (!util::VectorToObject(rp_id_hash, sign_req.appId,
                              sizeof(sign_req.appId))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_secret, sign_req.userSecret,
                              sizeof(sign_req.userSecret))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }
    if (!util::VectorToObject(credential_id, &sign_req.keyHandle,
                              sizeof(sign_req.keyHandle))) {
      return HasCredentialsResponse::INVALID_REQUEST;
    }

    struct u2f_sign_resp sign_resp;

    sign_status = tpm_proxy_->SendU2fSign(sign_req, &sign_resp);
    brillo::SecureClearContainer(sign_req.userSecret);
  } else {
    return HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID;
  }

  // Return status of 0 indicates the credential is valid.
  return (sign_status == 0) ? HasCredentialsResponse::SUCCESS
                            : HasCredentialsResponse::UNKNOWN_CREDENTIAL_ID;
}

MakeCredentialResponse::MakeCredentialStatus U2fCommandProcessorGsc::G2fAttest(
    const std::vector<uint8_t>& data,
    const brillo::SecureBlob& secret,
    uint8_t format,
    std::vector<uint8_t>* signature_out) {
  struct u2f_attest_req attest_req = {
      .format = format, .dataLen = static_cast<uint8_t>(data.size())};
  if (!util::VectorToObject(secret, attest_req.userSecret,
                            sizeof(attest_req.userSecret))) {
    return MakeCredentialResponse::INTERNAL_ERROR;
  }
  if (!util::VectorToObject(data, attest_req.data, sizeof(attest_req.data))) {
    return MakeCredentialResponse::INTERNAL_ERROR;
  }

  struct u2f_attest_resp attest_resp = {};
  uint32_t attest_status = tpm_proxy_->SendU2fAttest(attest_req, &attest_resp);

  brillo::SecureClearBytes(&attest_req.userSecret,
                           sizeof(attest_req.userSecret));

  if (attest_status != 0) {
    // We are attesting to a key handle that we just created, so if
    // attestation fails we have hit some internal error.
    LOG(ERROR) << "U2F_ATTEST failed, status: " << std::hex
               << static_cast<uint32_t>(attest_status);
    return MakeCredentialResponse::INTERNAL_ERROR;
  }

  base::Optional<std::vector<uint8_t>> signature =
      util::SignatureToDerBytes(attest_resp.sig_r, attest_resp.sig_s);

  if (!signature.has_value()) {
    LOG(ERROR) << "DER encoding of U2F_ATTEST signature failed.";
    return MakeCredentialResponse::INTERNAL_ERROR;
  }

  *signature_out = *signature;

  return MakeCredentialResponse::SUCCESS;
}

base::Optional<std::vector<uint8_t>> U2fCommandProcessorGsc::GetG2fCert() {
  std::string cert_str;
  std::vector<uint8_t> cert;

  uint32_t get_cert_status = tpm_proxy_->GetG2fCertificate(&cert_str);

  if (get_cert_status != 0) {
    LOG(ERROR) << "Failed to retrieve G2F certificate, status: " << std::hex
               << get_cert_status;
    return base::nullopt;
  }

  util::AppendToVector(cert_str, &cert);

  if (!util::RemoveCertificatePadding(&cert)) {
    LOG(ERROR) << "Failed to remove padding from G2F certificate ";
    return base::nullopt;
  }

  return cert;
}

CoseAlgorithmIdentifier U2fCommandProcessorGsc::GetAlgorithm() {
  return CoseAlgorithmIdentifier::kEs256;
}

// This is needed for backward compatibility. Credential id's that were already
// generated have inserted hash, so we continue to insert/remove them.
void U2fCommandProcessorGsc::InsertAuthTimeSecretHashToCredentialId(
    const brillo::Blob* auth_time_secret_hash, std::vector<uint8_t>* input) {
  CHECK(input->size() == U2F_V1_KH_SIZE);
  // The auth time secret hash should be inserted right after the header and
  // the authorization salt, before the authorization hmac.
  input->insert(
      input->cbegin() + offsetof(u2f_versioned_key_handle, authorization_hmac),
      auth_time_secret_hash->cbegin(), auth_time_secret_hash->cend());
}

// This is needed for backward compatibility. Credential id's that were already
// generated have inserted hash, so we continue to insert/remove them.
void U2fCommandProcessorGsc::RemoveAuthTimeSecretHashFromCredentialId(
    std::vector<uint8_t>* input) {
  CHECK_EQ(input->size(), U2F_V1_KH_SIZE + SHA256_DIGEST_LENGTH);
  // The auth time secret hash is after the header and the authorization salt,
  // before the authorization hmac. Remove it so that cr50 recognizes the KH.
  const std::vector<uint8_t>::const_iterator remove_begin =
      input->cbegin() + offsetof(u2f_versioned_key_handle, authorization_hmac);
  input->erase(remove_begin, remove_begin + SHA256_DIGEST_LENGTH);
}

template <typename Response>
MakeCredentialResponse::MakeCredentialStatus
U2fCommandProcessorGsc::SendU2fGenerateWaitForPresence(
    struct u2f_generate_req* generate_req,
    Response* generate_resp,
    std::vector<uint8_t>* credential_id,
    std::vector<uint8_t>* credential_public_key) {
  uint32_t generate_status = -1;

  CallAndWaitForPresence(
      [this, generate_req, generate_resp]() {
        return tpm_proxy_->SendU2fGenerate(*generate_req, generate_resp);
      },
      &generate_status);
  brillo::SecureClearContainer(generate_req->userSecret);

  if (generate_status == 0) {
    std::vector<uint8_t> public_key;
    util::AppendToVector(generate_resp->pubKey, &public_key);

    util::AppendToVector(EncodeCredentialPublicKeyInCBOR(public_key),
                         credential_public_key);
    util::AppendToVector(generate_resp->keyHandle, credential_id);
    return MakeCredentialResponse::SUCCESS;
  }

  return MakeCredentialResponse::VERIFICATION_FAILED;
}

template <typename Request>
GetAssertionResponse::GetAssertionStatus
U2fCommandProcessorGsc::SendU2fSignWaitForPresence(
    Request* sign_req,
    struct u2f_sign_resp* sign_resp,
    std::vector<uint8_t>* signature) {
  uint32_t sign_status = -1;

  CallAndWaitForPresence(
      [this, sign_req, sign_resp]() {
        return tpm_proxy_->SendU2fSign(*sign_req, sign_resp);
      },
      &sign_status);
  brillo::SecureClearContainer(sign_req->userSecret);

  if (sign_status == 0) {
    base::Optional<std::vector<uint8_t>> opt_signature =
        util::SignatureToDerBytes(sign_resp->sig_r, sign_resp->sig_s);
    if (!opt_signature.has_value()) {
      return GetAssertionResponse::INTERNAL_ERROR;
    }
    *signature = *opt_signature;
    return GetAssertionResponse::SUCCESS;
  }

  return GetAssertionResponse::VERIFICATION_FAILED;
}

void U2fCommandProcessorGsc::CallAndWaitForPresence(
    std::function<uint32_t()> fn, uint32_t* status) {
  *status = fn();
  base::TimeTicks verification_start = base::TimeTicks::Now();
  while (*status == kCr50StatusNotAllowed &&
         base::TimeTicks::Now() - verification_start < kVerificationTimeout) {
    // We need user presence. Show a notification requesting it, and try again.
    // We have a delay in request_presence_, so we didn't need to sleep again.
    request_presence_();
    *status = fn();
  }
}

std::vector<uint8_t> U2fCommandProcessorGsc::EncodeCredentialPublicKeyInCBOR(
    const std::vector<uint8_t>& credential_public_key) {
  DCHECK_EQ(credential_public_key.size(), sizeof(struct u2f_ec_point));
  cbor::Value::MapValue cbor_map;
  cbor_map[cbor::Value(kCoseKeyKtyLabel)] = cbor::Value(kCoseKeyKtyEC2);
  cbor_map[cbor::Value(kCoseKeyAlgLabel)] = cbor::Value(kCoseKeyAlgES256);
  cbor_map[cbor::Value(kCoseECKeyCrvLabel)] = cbor::Value(1);
  cbor_map[cbor::Value(kCoseECKeyXLabel)] = cbor::Value(base::make_span(
      credential_public_key.data() + offsetof(struct u2f_ec_point, x),
      U2F_EC_KEY_SIZE));
  cbor_map[cbor::Value(kCoseECKeyYLabel)] = cbor::Value(base::make_span(
      credential_public_key.data() + offsetof(struct u2f_ec_point, y),
      U2F_EC_KEY_SIZE));
  return *cbor::Writer::Write(cbor::Value(std::move(cbor_map)));
}

}  // namespace u2f
