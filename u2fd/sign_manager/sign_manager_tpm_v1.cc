// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/sign_manager/sign_manager_tpm_v1.h"

#include <string>
#include <utility>
#include <vector>

#include <brillo/secure_blob.h>
#include <chromeos/cbor/values.h>
#include <chromeos/cbor/writer.h>
#include <tpm_manager/client/tpm_manager_utility.h>
#include <trousers/scoped_tss_type.h>
#include <trousers/trousers.h>
#include <trousers/tss.h>

#define TPM_LOG(severity, result)                               \
  LOG(severity) << "TPM error 0x" << std::hex << result << " (" \
                << Trspi_Error_String(result) << "): "

namespace u2f {

namespace {

using brillo::SecureBlob;
using trousers::ScopedTssContext;
using trousers::ScopedTssKey;
using trousers::ScopedTssMemory;
using trousers::ScopedTssPcrs;
using trousers::ScopedTssPolicy;

using ScopedTssHash = trousers::ScopedTssObject<TSS_HHASH>;

// COSE key parameters.
// https://tools.ietf.org/html/rfc8152#section-7.1
const int kCoseKeyKtyLabel = 1;
const int kCoseKeyKtyRsa = 3;
const int kCoseKeyAlgLabel = 3;
const int kCoseKeyAlgRs256 = -257;

// COSE key type parameters.
// https://tools.ietf.org/html/rfc8152#section-13.1.1
const int kCoseRsaKeyNLabel = -1;
const int kCoseRsaKeyELabel = -2;

constexpr unsigned char kSha256DigestInfo[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
    0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20};
std::vector<uint8_t> EncodeCredentialPublicKeyInCBOR(
    const std::string& public_exponent, const std::string& modulus) {
  cbor::Value::MapValue cbor_map;
  cbor_map[cbor::Value(kCoseKeyKtyLabel)] = cbor::Value(kCoseKeyKtyRsa);
  cbor_map[cbor::Value(kCoseKeyAlgLabel)] = cbor::Value(kCoseKeyAlgRs256);
  cbor_map[cbor::Value(kCoseRsaKeyNLabel)] =
      cbor::Value(modulus, cbor::Value::Type::BYTE_STRING);
  cbor_map[cbor::Value(kCoseRsaKeyELabel)] =
      cbor::Value(public_exponent, cbor::Value::Type::BYTE_STRING);
  return *cbor::Writer::Write(cbor::Value(std::move(cbor_map)));
}

BYTE* StringAsTSSBuffer(std::string* s) {
  return reinterpret_cast<BYTE*>(std::data(*s));
}

std::string TSSBufferAsString(const BYTE* buffer, size_t length) {
  return std::string(reinterpret_cast<const char*>(buffer), length);
}

}  // namespace

SignManagerTpmV1::SignManagerTpmV1() : tpm_manager_utility_(nullptr) {}

bool SignManagerTpmV1::IsReady() {
  return SetupSrk();
}

bool SignManagerTpmV1::Sign(const std::string& key_blob,
                            const std::string& data_to_sign,
                            const brillo::SecureBlob& auth_data,
                            std::string* signature_der) {
  if (!SetupSrk()) {
    LOG(ERROR) << "SRK is not ready.";
    return false;
  }
  // Load key before signing.
  ScopedTssKey key(context_handle_);
  std::string mutable_key_blob(key_blob);
  BYTE* key_blob_buffer = StringAsTSSBuffer(&mutable_key_blob);
  TSS_RESULT result =
      Tspi_Context_LoadKeyByBlob(context_handle_, srk_handle_, key_blob.size(),
                                 key_blob_buffer, key.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to load key.";
    return false;
  }

  if (!CreateKeyPolicy(key, auth_data, true)) {
    return false;
  }

  // Construct an ASN.1 DER DigestInfo.
  std::string digest_to_sign(std::begin(kSha256DigestInfo),
                             std::end(kSha256DigestInfo));
  digest_to_sign += data_to_sign;
  // Create a hash object to hold the digest.
  ScopedTssHash hash_handle(context_handle_);
  result = Tspi_Context_CreateObject(context_handle_, TSS_OBJECT_TYPE_HASH,
                                     TSS_HASH_OTHER, hash_handle.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to create hash object.";
    return false;
  }
  result = Tspi_Hash_SetHashValue(hash_handle, digest_to_sign.size(),
                                  StringAsTSSBuffer(&digest_to_sign));
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to set hash data.";
    return false;
  }
  UINT32 length = 0;
  ScopedTssMemory buffer(context_handle_);
  result = Tspi_Hash_Sign(hash_handle, key, &length, buffer.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to generate signature.";
    return false;
  }
  signature_der->assign(TSSBufferAsString(buffer.value(), length));
  return true;
}

bool SignManagerTpmV1::CreateKey(KeyType key_type,
                                 const brillo::SecureBlob& auth_data,
                                 std::string* key_blob,
                                 std::vector<uint8_t>* public_key_cbor) {
  if (!SetupSrk()) {
    LOG(ERROR) << "SRK is not ready.";
    return false;
  }
  if (key_type != KeyType::kRsa) {
    LOG(ERROR) << "Only RSA supported on TPM v1.2.";
    return false;
  }

  // Create a non-migratable RSA key.
  ScopedTssKey key(context_handle_);
  UINT32 init_flags = TSS_KEY_TYPE_SIGNING | TSS_KEY_NOT_MIGRATABLE |
                      TSS_KEY_VOLATILE | TSS_KEY_AUTHORIZATION |
                      TSS_KEY_SIZE_2048;
  TSS_RESULT result = Tspi_Context_CreateObject(
      context_handle_, TSS_OBJECT_TYPE_RSAKEY, init_flags, key.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to create object.";
    return false;
  }
  if (!CreateKeyPolicy(key, auth_data, false)) {
    return false;
  }

  result = Tspi_Key_CreateKey(key, srk_handle_, 0);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to create key.";
    return false;
  }
  result = Tspi_Key_LoadKey(key, srk_handle_);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to load key.";
    return false;
  }

  std::string public_exponent, modulus;
  // Get the public key.
  if (!GetDataAttribute(context_handle_, key, TSS_TSPATTRIB_RSAKEY_INFO,
                        TSS_TSPATTRIB_KEYINFO_RSA_EXPONENT, &public_exponent)) {
    LOG(ERROR) << __func__ << ": Failed to read public exponent.";
    return false;
  }
  if (!GetDataAttribute(context_handle_, key, TSS_TSPATTRIB_RSAKEY_INFO,
                        TSS_TSPATTRIB_KEYINFO_RSA_MODULUS, &modulus)) {
    LOG(ERROR) << __func__ << ": Failed to read modulus.";
    return false;
  }
  *public_key_cbor = EncodeCredentialPublicKeyInCBOR(public_exponent, modulus);

  // Get the key blob.
  if (!GetDataAttribute(context_handle_, key, TSS_TSPATTRIB_KEY_BLOB,
                        TSS_TSPATTRIB_KEYBLOB_BLOB, key_blob)) {
    LOG(ERROR) << __func__ << ": Failed to read key blob.";
    return false;
  }

  result = Tspi_Key_UnloadKey(key);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to unload key.";
    // Don't need to return false here since we already successfully created the
    // key and obtained all the data.
  }

  return true;
}

bool SignManagerTpmV1::CreateKeyPolicy(TSS_HKEY key,
                                       const SecureBlob& auth_data,
                                       bool auth_only) {
  ScopedTssPolicy policy(context_handle_);
  TSS_RESULT result = Tspi_Context_CreateObject(
      context_handle_, TSS_OBJECT_TYPE_POLICY, TSS_POLICY_USAGE, policy.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to create policy.";
    return false;
  }
  if (auth_data.empty()) {
    result = Tspi_Policy_SetSecret(policy, TSS_SECRET_MODE_NONE, 0, NULL);
  } else {
    result =
        Tspi_Policy_SetSecret(policy, TSS_SECRET_MODE_PLAIN, auth_data.size(),
                              const_cast<BYTE*>(auth_data.data()));
  }
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to set auth value for key.";
    return false;
  }

  if (!auth_only) {
    result = Tspi_SetAttribUint32(key, TSS_TSPATTRIB_KEY_INFO,
                                  TSS_TSPATTRIB_KEYINFO_SIGSCHEME,
                                  TSS_SS_RSASSAPKCS1V15_DER);
    if (TPM_ERROR(result)) {
      TPM_LOG(ERROR, result) << __func__ << ": Failed to set scheme.";
      return false;
    }
  }

  result = Tspi_Policy_AssignToObject(policy.release(), key);
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << ": Failed to assign policy to key.";
    return false;
  }
  return true;
}

bool SignManagerTpmV1::IsTpmReady() {
  if (is_ready_) {
    return true;
  }
  tpm_manager::LocalData local_data;
  bool is_enabled = false;
  bool is_owned = false;
  if (!tpm_manager_utility_) {
    tpm_manager_utility_ = tpm_manager::TpmManagerUtility::GetSingleton();
    if (!tpm_manager_utility_) {
      LOG(ERROR) << __func__ << ": Failed to get tpm_manager utility.";
      return false;
    }
  }
  if (!tpm_manager_utility_->GetTpmStatus(&is_enabled, &is_owned,
                                          &local_data)) {
    LOG(ERROR) << __func__ << ": Failed to get tpm status from tpm_manager.";
    return false;
  }
  is_ready_ = is_enabled && is_owned;
  return is_ready_;
}

bool SignManagerTpmV1::ConnectContextAsUser(ScopedTssContext* context,
                                            TSS_HTPM* tpm) {
  *tpm = 0;
  TSS_RESULT result;
  if (TPM_ERROR(result = Tspi_Context_Create(context->ptr()))) {
    TPM_LOG(ERROR, result) << __func__ << ": Error calling Tspi_Context_Create";
    return false;
  }
  if (TPM_ERROR(result = Tspi_Context_Connect(*context, nullptr))) {
    TPM_LOG(ERROR, result) << __func__
                           << ": Error calling Tspi_Context_Connect";
    return false;
  }
  if (TPM_ERROR(result = Tspi_Context_GetTpmObject(*context, tpm))) {
    TPM_LOG(ERROR, result) << __func__
                           << ": Error calling Tspi_Context_GetTpmObject";
    return false;
  }
  return true;
}

bool SignManagerTpmV1::SetupSrk() {
  if (!IsTpmReady()) {
    return false;
  }
  if (srk_handle_) {
    return true;
  }
  if (!InitializeContextHandle(__func__)) {
    return false;
  }
  srk_handle_.reset(context_handle_, 0);
  if (!LoadSrk(context_handle_, &srk_handle_)) {
    LOG(ERROR) << __func__ << ": Failed to load SRK.";
    return false;
  }
  // In order to wrap a key with the SRK we need access to the SRK public key
  // and we need to get it manually. Once it's in the key object, we don't need
  // to do this again.
  UINT32 length = 0;
  ScopedTssMemory buffer(context_handle_);
  TSS_RESULT result;
  result = Tspi_Key_GetPubKey(srk_handle_, &length, buffer.ptr());
  if (result != TSS_SUCCESS) {
    TPM_LOG(INFO, result) << __func__ << ": Failed to read SRK public key.";
    return false;
  }
  return true;
}

bool SignManagerTpmV1::LoadSrk(TSS_HCONTEXT context_handle,
                               ScopedTssKey* srk_handle) {
  TSS_RESULT result;
  TSS_UUID uuid = TSS_UUID_SRK;
  if (TPM_ERROR(result = Tspi_Context_LoadKeyByUUID(context_handle,
                                                    TSS_PS_TYPE_SYSTEM, uuid,
                                                    srk_handle->ptr()))) {
    TPM_LOG(ERROR, result) << __func__
                           << ": Error calling Tspi_Context_LoadKeyByUUID";
    return false;
  }
  // Check if the SRK wants a password.
  UINT32 auth_usage;
  if (TPM_ERROR(result = Tspi_GetAttribUint32(
                    *srk_handle, TSS_TSPATTRIB_KEY_INFO,
                    TSS_TSPATTRIB_KEYINFO_AUTHUSAGE, &auth_usage))) {
    TPM_LOG(ERROR, result) << __func__
                           << ": Error calling Tspi_GetAttribUint32";
    return false;
  }
  if (auth_usage) {
    // Give it an empty password if needed.
    TSS_HPOLICY usage_policy;
    if (TPM_ERROR(result = Tspi_GetPolicyObject(*srk_handle, TSS_POLICY_USAGE,
                                                &usage_policy))) {
      TPM_LOG(ERROR, result)
          << __func__ << ": Error calling Tspi_GetPolicyObject";
      return false;
    }

    BYTE empty_password[] = {};
    if (TPM_ERROR(result =
                      Tspi_Policy_SetSecret(usage_policy, TSS_SECRET_MODE_PLAIN,
                                            0, empty_password))) {
      TPM_LOG(ERROR, result)
          << __func__ << ": Error calling Tspi_Policy_SetSecret";
      return false;
    }
  }
  return true;
}

bool SignManagerTpmV1::GetDataAttribute(TSS_HCONTEXT context,
                                        TSS_HOBJECT object,
                                        TSS_FLAG flag,
                                        TSS_FLAG sub_flag,
                                        std::string* data) {
  UINT32 length = 0;
  ScopedTssMemory buffer(context);
  TSS_RESULT result =
      Tspi_GetAttribData(object, flag, sub_flag, &length, buffer.ptr());
  if (TPM_ERROR(result)) {
    TPM_LOG(ERROR, result) << __func__ << "Failed to read object attribute.";
    return false;
  }
  data->assign(TSSBufferAsString(buffer.value(), length));
  return true;
}

bool SignManagerTpmV1::InitializeContextHandle(
    const std::string& consumer_name) {
  if (!static_cast<TSS_HCONTEXT>(context_handle_) || !tpm_handle_) {
    context_handle_.reset();
    if (!ConnectContextAsUser(&context_handle_, &tpm_handle_)) {
      LOG(ERROR) << __func__ << ": Failed to connect to the TPM.";
      return false;
    }
  }
  return true;
}

}  // namespace u2f
