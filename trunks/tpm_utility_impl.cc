// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/tpm_utility_impl.h"

#include <base/logging.h>
#include <base/memory/scoped_ptr.h>
#include <base/sha1.h>
#include <base/stl_util.h>
#include <crypto/secure_hash.h>
#include <crypto/sha2.h>
#include <openssl/aes.h>
#include <openssl/rand.h>

#include "trunks/authorization_delegate.h"
#include "trunks/error_codes.h"
#include "trunks/hmac_authorization_delegate.h"
#include "trunks/hmac_session.h"
#include "trunks/scoped_key_handle.h"
#include "trunks/tpm_constants.h"
#include "trunks/tpm_state.h"
#include "trunks/trunks_factory.h"

namespace {

const char kPlatformPassword[] = "cros-platform";
const char kWellKnownPassword[] = "cros-password";
const size_t kMaxPasswordLength = 32;
// The below maximum is defined in TPM 2.0 Library Spec Part 2 Section 13.1
const uint32_t kMaxNVSpaceIndex = (1<<24) - 1;

// Returns a serialized representation of the unmodified handle. This is useful
// for predefined handle values, like TPM_RH_OWNER. For details on what types of
// handles use this name formula see Table 3 in the TPM 2.0 Library Spec Part 1
// (Section 16 - Names).
std::string NameFromHandle(trunks::TPM_HANDLE handle) {
  std::string name;
  trunks::Serialize_TPM_HANDLE(handle, &name);
  return name;
}

std::string HashString(const std::string& plaintext,
                       trunks::TPM_ALG_ID hash_alg) {
  switch (hash_alg) {
    case trunks::TPM_ALG_SHA1:
      return base::SHA1HashString(plaintext);
    case trunks::TPM_ALG_SHA256:
      return crypto::SHA256HashString(plaintext);
  }
  NOTREACHED();
  return std::string();
}

}  // namespace

namespace trunks {

TpmUtilityImpl::TpmUtilityImpl(const TrunksFactory& factory)
    : factory_(factory) {}

TpmUtilityImpl::~TpmUtilityImpl() {
}

TPM_RC TpmUtilityImpl::Startup() {
  TPM_RC result = TPM_RC_SUCCESS;
  Tpm* tpm = factory_.GetTpm();
  result = tpm->StartupSync(TPM_SU_CLEAR, nullptr);
  // Ignore TPM_RC_INITIALIZE, that means it was already started.
  if (result && result != TPM_RC_INITIALIZE) {
    LOG(ERROR) << __func__ << ": " << GetErrorString(result);
    return result;
  }
  result = tpm->SelfTestSync(YES /* Full test. */, nullptr);
  if (result) {
    LOG(ERROR) << __func__ << ": " << GetErrorString(result);
    return result;
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::Clear() {
  TPM_RC result = TPM_RC_SUCCESS;
  scoped_ptr<AuthorizationDelegate> password_delegate(
      factory_.GetPasswordAuthorization(""));
  result = factory_.GetTpm()->ClearSync(TPM_RH_PLATFORM,
                                        NameFromHandle(TPM_RH_PLATFORM),
                                        password_delegate.get());
  // If there was an error in the initialization, platform auth is in a bad
  // state.
  if (result == TPM_RC_AUTH_MISSING) {
    scoped_ptr<AuthorizationDelegate> authorization(
        factory_.GetPasswordAuthorization(kPlatformPassword));
    result = factory_.GetTpm()->ClearSync(TPM_RH_PLATFORM,
                                          NameFromHandle(TPM_RH_PLATFORM),
                                          authorization.get());
  }
  if (GetFormatOneError(result) == TPM_RC_BAD_AUTH) {
    LOG(INFO) << "Clear failed because of BAD_AUTH. This probably means "
              << "that the TPM was already initialized.";
    return result;
  }
  if (result) {
    LOG(ERROR) << "Failed to clear the TPM: " << GetErrorString(result);
  }
  return result;
}

void TpmUtilityImpl::Shutdown() {
  TPM_RC return_code = factory_.GetTpm()->ShutdownSync(TPM_SU_CLEAR, nullptr);
  if (return_code && return_code != TPM_RC_INITIALIZE) {
    // This should not happen, but if it does, there is nothing we can do.
    LOG(ERROR) << "Error shutting down: " << GetErrorString(return_code);
  }
}

TPM_RC TpmUtilityImpl::InitializeTpm() {
  TPM_RC result = TPM_RC_SUCCESS;
  scoped_ptr<TpmState> tpm_state(factory_.GetTpmState());
  result = tpm_state->Initialize();
  if (result) {
    LOG(ERROR) << __func__ << ": " << GetErrorString(result);
    return result;
  }
  // Warn about various unexpected conditions.
  if (!tpm_state->WasShutdownOrderly()) {
    LOG(WARNING) << "WARNING: The last TPM shutdown was not orderly.";
  }
  if (tpm_state->IsInLockout()) {
    LOG(WARNING) << "WARNING: The TPM is currently in lockout.";
  }

  // We expect the firmware has already locked down the platform hierarchy. If
  // it hasn't, do it now.
  if (tpm_state->IsPlatformHierarchyEnabled()) {
    scoped_ptr<AuthorizationDelegate> empty_password(
        factory_.GetPasswordAuthorization(""));
    result = SetHierarchyAuthorization(TPM_RH_PLATFORM,
                                       kPlatformPassword,
                                       empty_password.get());
    if (GetFormatOneError(result) == TPM_RC_BAD_AUTH) {
      // Most likely the platform password has already been set.
      result = TPM_RC_SUCCESS;
    }
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << __func__ << ": " << GetErrorString(result);
      return result;
    }
    result = AllocatePCR(kPlatformPassword);
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << __func__ << ": " << GetErrorString(result);
      return result;
    }
    scoped_ptr<AuthorizationDelegate> authorization(
        factory_.GetPasswordAuthorization(kPlatformPassword));
    result = DisablePlatformHierarchy(authorization.get());
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << __func__ << ": " << GetErrorString(result);
      return result;
    }
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::AllocatePCR(const std::string& platform_password) {
  TPM_RC result;
  TPML_PCR_SELECTION pcr_allocation;
  pcr_allocation.count = 1;
  pcr_allocation.pcr_selections[0].hash = TPM_ALG_SHA256;
  pcr_allocation.pcr_selections[0].sizeof_select = PCR_SELECT_MIN;
  pcr_allocation.pcr_selections[0].pcr_select[0] = 0xff;
  pcr_allocation.pcr_selections[0].pcr_select[1] = 0xff;
  scoped_ptr<AuthorizationDelegate> platform_delegate(
      factory_.GetPasswordAuthorization(platform_password));
  TPMI_YES_NO allocation_success;
  uint32_t max_pcr;
  uint32_t size_needed;
  uint32_t size_available;
  result = factory_.GetTpm()->PCR_AllocateSync(TPM_RH_PLATFORM,
                                               NameFromHandle(TPM_RH_PLATFORM),
                                               pcr_allocation,
                                               &allocation_success,
                                               &max_pcr,
                                               &size_needed,
                                               &size_available,
                                               platform_delegate.get());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error allocating pcr: " << GetErrorString(result);
    return result;
  }
  if (allocation_success != YES) {
    LOG(ERROR) << "PCR allocation unsuccessful.";
    return TPM_RC_FAILURE;
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::TakeOwnership(const std::string& owner_password,
                                     const std::string& endorsement_password,
                                     const std::string& lockout_password) {
  // First we set the storage hierarchy authorization to the well know default
  // password.
  TPM_RC result = TPM_RC_SUCCESS;
  result = SetKnownOwnerPassword(kWellKnownPassword);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error injecting known password: " << GetErrorString(result);
    return result;
  }

  result = CreateStorageRootKeys(kWellKnownPassword);
  if (result) {
    LOG(ERROR) << "Error creating SRKs: " << GetErrorString(result);
    return result;
  }
  result = CreateSaltingKey(kWellKnownPassword);
  if (result) {
    LOG(ERROR) << "Error creating salting key: "
               << GetErrorString(result);
    return result;
  }

  scoped_ptr<HmacSession> session = factory_.GetHmacSession();
  result = session->StartUnboundSession(true);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error initializing AuthorizationSession: "
               << GetErrorString(result);
    return result;
  }
  scoped_ptr<TpmState> tpm_state(factory_.GetTpmState());
  result = tpm_state->Initialize();
  session->SetEntityAuthorizationValue("");
  session->SetFutureAuthorizationValue(endorsement_password);
  if (!tpm_state->IsEndorsementPasswordSet()) {
    result = SetHierarchyAuthorization(TPM_RH_ENDORSEMENT,
                                       endorsement_password,
                                       session->GetDelegate());
    if (result) {
      LOG(ERROR) << __func__ << ": " << GetErrorString(result);
      return result;
    }
  }
  session->SetFutureAuthorizationValue(lockout_password);
  if (!tpm_state->IsLockoutPasswordSet()) {
    result = SetHierarchyAuthorization(TPM_RH_LOCKOUT,
                                       lockout_password,
                                       session->GetDelegate());
    if (result) {
      LOG(ERROR) << __func__ << ": " << GetErrorString(result);
      return result;
    }
  }
  // We take ownership of owner hierarchy last.
  session->SetEntityAuthorizationValue(kWellKnownPassword);
  session->SetFutureAuthorizationValue(owner_password);
  result = SetHierarchyAuthorization(TPM_RH_OWNER,
                                     owner_password,
                                     session->GetDelegate());
  if ((GetFormatOneError(result) == TPM_RC_BAD_AUTH) &&
      tpm_state->IsOwnerPasswordSet()) {
    LOG(WARNING) << "Error changing owner password. This probably because "
                 << "ownership is already taken.";
    return TPM_RC_SUCCESS;
  } else if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error changing owner authorization: "
               << GetErrorString(result);
    return result;
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::StirRandom(const std::string& entropy_data,
                                  AuthorizationDelegate* delegate) {
  std::string digest = crypto::SHA256HashString(entropy_data);
  TPM2B_SENSITIVE_DATA random_bytes = Make_TPM2B_SENSITIVE_DATA(digest);
  return factory_.GetTpm()->StirRandomSync(random_bytes, delegate);
}

TPM_RC TpmUtilityImpl::GenerateRandom(size_t num_bytes,
                                      AuthorizationDelegate* delegate,
                                      std::string* random_data) {
  CHECK(random_data);
  size_t bytes_left = num_bytes;
  random_data->clear();
  TPM_RC rc;
  TPM2B_DIGEST digest;
  while (bytes_left > 0) {
    rc = factory_.GetTpm()->GetRandomSync(bytes_left,
                                          &digest,
                                          delegate);
    if (rc) {
      LOG(ERROR) << "Error getting random data from tpm.";
      return rc;
    }
    random_data->append(StringFrom_TPM2B_DIGEST(digest));
    bytes_left -= digest.size;
  }
  CHECK_EQ(random_data->size(), num_bytes);
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::ExtendPCR(int pcr_index,
                                 const std::string& extend_data,
                                 AuthorizationDelegate* delegate) {
  if (pcr_index < 0 || pcr_index >= IMPLEMENTATION_PCR) {
    LOG(ERROR) << "Using a PCR index that isnt implemented.";
    return TPM_RC_FAILURE;
  }
  TPM_HANDLE pcr_handle = HR_PCR + pcr_index;
  std::string pcr_name = NameFromHandle(pcr_handle);
  TPML_DIGEST_VALUES digests;
  digests.count = 1;
  digests.digests[0].hash_alg = TPM_ALG_SHA256;
  crypto::SHA256HashString(extend_data,
                           digests.digests[0].digest.sha256,
                           crypto::kSHA256Length);
  return factory_.GetTpm()->PCR_ExtendSync(pcr_handle,
                                           pcr_name,
                                           digests,
                                           delegate);
}

TPM_RC TpmUtilityImpl::ReadPCR(int pcr_index, std::string* pcr_value) {
  TPML_PCR_SELECTION pcr_select_in;
  uint32_t pcr_update_counter;
  TPML_PCR_SELECTION pcr_select_out;
  TPML_DIGEST pcr_values;
  // This process of selecting pcrs is highlighted in TPM 2.0 Library Spec
  // Part 2 (Section 10.5 - PCR structures).
  uint8_t pcr_select_index = pcr_index / 8;
  uint8_t pcr_select_byte = 1 << (pcr_index % 8);
  pcr_select_in.count = 1;
  pcr_select_in.pcr_selections[0].hash = TPM_ALG_SHA256;
  pcr_select_in.pcr_selections[0].sizeof_select = PCR_SELECT_MIN;
  pcr_select_in.pcr_selections[0].pcr_select[pcr_select_index] =
      pcr_select_byte;

  TPM_RC rc = factory_.GetTpm()->PCR_ReadSync(pcr_select_in,
                                              &pcr_update_counter,
                                              &pcr_select_out,
                                              &pcr_values,
                                              nullptr);
  if (rc) {
    LOG(INFO) << "Error trying to read a pcr: " << GetErrorString(rc);
    return rc;
  }
  if (pcr_select_out.count != 1 ||
      pcr_select_out.pcr_selections[0].sizeof_select <
      (pcr_select_index + 1) ||
      pcr_select_out.pcr_selections[0].pcr_select[pcr_select_index] !=
      pcr_select_byte) {
    LOG(ERROR) << "TPM did not return the requested PCR";
    return TPM_RC_FAILURE;
  }
  CHECK_GE(pcr_values.count, 1U);
  pcr_value->assign(StringFrom_TPM2B_DIGEST(pcr_values.digests[0]));
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::AsymmetricEncrypt(TPM_HANDLE key_handle,
                                         TPM_ALG_ID scheme,
                                         TPM_ALG_ID hash_alg,
                                         const std::string& plaintext,
                                         AuthorizationDelegate* delegate,
                                         std::string* ciphertext) {
  TPMT_RSA_DECRYPT in_scheme;
  if (hash_alg == TPM_ALG_NULL) {
    hash_alg = TPM_ALG_SHA256;
  }
  if (scheme == TPM_ALG_RSAES) {
    in_scheme.scheme = TPM_ALG_RSAES;
  } else if (scheme == TPM_ALG_OAEP || scheme == TPM_ALG_NULL) {
    in_scheme.scheme = TPM_ALG_OAEP;
    in_scheme.details.oaep.hash_alg = hash_alg;
  } else {
    LOG(ERROR) << "Invalid Signing scheme used.";
    return SAPI_RC_BAD_PARAMETER;
  }

  TPMT_PUBLIC public_area;
  TPM_RC result = GetKeyPublicArea(key_handle, &public_area);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error finding public area for: " << key_handle;
    return result;
  } else if (public_area.type != TPM_ALG_RSA) {
    LOG(ERROR) << "Key handle given is not an RSA key";
    return SAPI_RC_BAD_PARAMETER;
  } else if ((public_area.object_attributes & kDecrypt) == 0) {
    LOG(ERROR) << "Key handle given is not a decryption key";
    return SAPI_RC_BAD_PARAMETER;
  }
  if ((public_area.object_attributes & kRestricted) != 0) {
    LOG(ERROR) << "Cannot use RSAES for encryption with a restricted key";
    return SAPI_RC_BAD_PARAMETER;
  }
  std::string key_name;
  result = ComputeKeyName(public_area, &key_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error computing key name for: " << key_handle;
    return result;
  }

  TPM2B_DATA label;
  label.size = 0;
  TPM2B_PUBLIC_KEY_RSA in_message = Make_TPM2B_PUBLIC_KEY_RSA(plaintext);
  TPM2B_PUBLIC_KEY_RSA out_message;
  result = factory_.GetTpm()->RSA_EncryptSync(key_handle,
                                              key_name,
                                              in_message,
                                              in_scheme,
                                              label,
                                              &out_message,
                                              delegate);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error performing RSA encrypt: "
               << GetErrorString(result);
    return result;
  }
  ciphertext->assign(StringFrom_TPM2B_PUBLIC_KEY_RSA(out_message));
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::AsymmetricDecrypt(TPM_HANDLE key_handle,
                                         TPM_ALG_ID scheme,
                                         TPM_ALG_ID hash_alg,
                                         const std::string& ciphertext,
                                         AuthorizationDelegate* delegate,
                                         std::string* plaintext) {
  TPMT_RSA_DECRYPT in_scheme;
  if (hash_alg == TPM_ALG_NULL) {
    hash_alg = TPM_ALG_SHA256;
  }
  if (scheme == TPM_ALG_RSAES) {
    in_scheme.scheme = TPM_ALG_RSAES;
  } else if (scheme == TPM_ALG_OAEP || scheme == TPM_ALG_NULL) {
    in_scheme.scheme = TPM_ALG_OAEP;
    in_scheme.details.oaep.hash_alg = hash_alg;
  } else {
    LOG(ERROR) << "Invalid Signing scheme used.";
    return SAPI_RC_BAD_PARAMETER;
  }
  TPM_RC result;
  if (delegate == nullptr) {
    result = SAPI_RC_INVALID_SESSIONS;
    LOG(ERROR) << "This method needs a valid authorization delegate: "
               << GetErrorString(result);
    return result;
  }
  TPMT_PUBLIC public_area;
  result = GetKeyPublicArea(key_handle, &public_area);
  if (result) {
    LOG(ERROR) << "Error finding public area for: " << key_handle;
    return result;
  } else if (public_area.type != TPM_ALG_RSA) {
    LOG(ERROR) << "Key handle given is not an RSA key";
    return SAPI_RC_BAD_PARAMETER;
  } else if ((public_area.object_attributes & kDecrypt) == 0) {
    LOG(ERROR) << "Key handle given is not a decryption key";
    return SAPI_RC_BAD_PARAMETER;
  }
  if ((public_area.object_attributes & kRestricted) != 0) {
    LOG(ERROR) << "Cannot use RSAES for encryption with a restricted key";
    return SAPI_RC_BAD_PARAMETER;
  }
  std::string key_name;
  result = ComputeKeyName(public_area, &key_name);
  if (result) {
    LOG(ERROR) << "Error computing key name for: " << key_handle;
    return result;
  }

  TPM2B_DATA label;
  label.size = 0;
  TPM2B_PUBLIC_KEY_RSA in_message = Make_TPM2B_PUBLIC_KEY_RSA(ciphertext);
  TPM2B_PUBLIC_KEY_RSA out_message;
  result = factory_.GetTpm()->RSA_DecryptSync(key_handle,
                                              key_name,
                                              in_message,
                                              in_scheme,
                                              label,
                                              &out_message,
                                              delegate);
  if (result) {
    LOG(ERROR) << "Error performing RSA decrypt: "
               << GetErrorString(result);
    return result;
  }
  plaintext->assign(StringFrom_TPM2B_PUBLIC_KEY_RSA(out_message));
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::Sign(TPM_HANDLE key_handle,
                            TPM_ALG_ID scheme,
                            TPM_ALG_ID hash_alg,
                            const std::string& plaintext,
                            AuthorizationDelegate* delegate,
                            std::string* signature) {
  TPMT_SIG_SCHEME in_scheme;
  if (hash_alg == TPM_ALG_NULL) {
    hash_alg = TPM_ALG_SHA256;
  }
  if (scheme == TPM_ALG_RSAPSS) {
    in_scheme.scheme = TPM_ALG_RSAPSS;
    in_scheme.details.rsapss.hash_alg = hash_alg;
  } else if (scheme == TPM_ALG_RSASSA || scheme == TPM_ALG_NULL) {
    in_scheme.scheme = TPM_ALG_RSASSA;
    in_scheme.details.rsassa.hash_alg = hash_alg;
  } else {
    LOG(ERROR) << "Invalid Signing scheme used.";
    return SAPI_RC_BAD_PARAMETER;
  }
  TPM_RC result;
  if (delegate == nullptr) {
    result = SAPI_RC_INVALID_SESSIONS;
    LOG(ERROR) << "This method needs a valid authorization delegate: "
               << GetErrorString(result);
    return result;
  }
  TPMT_PUBLIC public_area;
  result = GetKeyPublicArea(key_handle, &public_area);
  if (result) {
    LOG(ERROR) << "Error finding public area for: " << key_handle;
    return result;
  } else if (public_area.type != TPM_ALG_RSA) {
    LOG(ERROR) << "Key handle given is not an RSA key";
    return SAPI_RC_BAD_PARAMETER;
  } else if ((public_area.object_attributes & kSign) == 0) {
    LOG(ERROR) << "Key handle given is not a signging key";
    return SAPI_RC_BAD_PARAMETER;
  } else if ((public_area.object_attributes & kRestricted) != 0) {
    LOG(ERROR) << "Key handle references a restricted key";
    return SAPI_RC_BAD_PARAMETER;
  }

  std::string key_name;
  result = ComputeKeyName(public_area, &key_name);
  if (result) {
    LOG(ERROR) << "Error computing key name for: " << key_handle;
    return result;
  }
  std::string digest = HashString(plaintext, hash_alg);
  TPM2B_DIGEST tpm_digest = Make_TPM2B_DIGEST(digest);
  TPMT_SIGNATURE signature_out;
  TPMT_TK_HASHCHECK validation;
  validation.tag = TPM_ST_HASHCHECK;
  validation.hierarchy = TPM_RH_NULL;
  validation.digest.size = 0;
  result = factory_.GetTpm()->SignSync(key_handle,
                                       key_name,
                                       tpm_digest,
                                       in_scheme,
                                       validation,
                                       &signature_out,
                                       delegate);
  if (result) {
    LOG(ERROR) << "Error signing digest: " << GetErrorString(result);
    return result;
  }
  if (scheme == TPM_ALG_RSAPSS) {
    signature->resize(signature_out.signature.rsapss.sig.size);
    signature->assign(StringFrom_TPM2B_PUBLIC_KEY_RSA(
        signature_out.signature.rsapss.sig));
  } else {
    signature->resize(signature_out.signature.rsassa.sig.size);
    signature->assign(StringFrom_TPM2B_PUBLIC_KEY_RSA(
        signature_out.signature.rsassa.sig));
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::Verify(TPM_HANDLE key_handle,
                              TPM_ALG_ID scheme,
                              TPM_ALG_ID hash_alg,
                              const std::string& plaintext,
                              const std::string& signature,
                              AuthorizationDelegate* delegate) {
  TPMT_PUBLIC public_area;
  TPM_RC return_code = GetKeyPublicArea(key_handle, &public_area);
  if (return_code) {
    LOG(ERROR) << "Error finding public area for: " << key_handle;
    return return_code;
  } else if (public_area.type != TPM_ALG_RSA) {
    LOG(ERROR) << "Key handle given is not an RSA key";
    return SAPI_RC_BAD_PARAMETER;
  } else if ((public_area.object_attributes & kSign) == 0) {
    LOG(ERROR) << "Key handle given is not a signing key";
    return SAPI_RC_BAD_PARAMETER;
  } else if ((public_area.object_attributes & kRestricted) != 0) {
    LOG(ERROR) << "Cannot use RSAPSS for signing with a restricted key";
    return SAPI_RC_BAD_PARAMETER;
  }
  if (hash_alg == TPM_ALG_NULL) {
    hash_alg = TPM_ALG_SHA256;
  }

  TPMT_SIGNATURE signature_in;
  if (scheme == TPM_ALG_RSAPSS) {
    signature_in.sig_alg = TPM_ALG_RSAPSS;
    signature_in.signature.rsapss.hash = hash_alg;
    signature_in.signature.rsapss.sig = Make_TPM2B_PUBLIC_KEY_RSA(signature);
  } else if (scheme == TPM_ALG_NULL || scheme == TPM_ALG_RSASSA) {
    signature_in.sig_alg = TPM_ALG_RSASSA;
    signature_in.signature.rsassa.hash = hash_alg;
    signature_in.signature.rsassa.sig = Make_TPM2B_PUBLIC_KEY_RSA(signature);
  } else {
    LOG(ERROR) << "Invalid scheme used to verify signature.";
    return SAPI_RC_BAD_PARAMETER;
  }
  std::string key_name;
  TPMT_TK_VERIFIED verified;
  std::string digest = HashString(plaintext, hash_alg);
  TPM2B_DIGEST tpm_digest = Make_TPM2B_DIGEST(digest);
  return_code = factory_.GetTpm()->VerifySignatureSync(key_handle,
                                                       key_name,
                                                       tpm_digest,
                                                       signature_in,
                                                       &verified,
                                                       delegate);
  if (return_code == TPM_RC_SIGNATURE) {
    LOG(WARNING) << "Incorrect signature for given digest.";
    return TPM_RC_SIGNATURE;
  } else if (return_code && return_code != TPM_RC_SIGNATURE) {
    LOG(ERROR) << "Error verifying signature: " << GetErrorString(return_code);
    return return_code;
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::ChangeKeyAuthorizationData(
    TPM_HANDLE key_handle,
    const std::string& new_password,
    AuthorizationDelegate* delegate,
    std::string* key_blob) {
  TPM_RC result;
  if (delegate == nullptr) {
    result = SAPI_RC_INVALID_SESSIONS;
    LOG(ERROR) << "This method needs a valid authorization delegate: "
               << GetErrorString(result);
    return result;
  }
  std::string key_name;
  std::string parent_name;
  result = GetKeyName(key_handle, &key_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting Key name for key_handle: "
               << GetErrorString(result);
    return result;
  }
  result = GetKeyName(kRSAStorageRootKey, &parent_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting Key name for RSA-SRK: "
               << GetErrorString(result);
    return result;
  }
  TPM2B_AUTH new_auth = Make_TPM2B_DIGEST(new_password);
  TPM2B_PRIVATE new_private_data;
  new_private_data.size = 0;
  result = factory_.GetTpm()->ObjectChangeAuthSync(key_handle,
                                                   key_name,
                                                   kRSAStorageRootKey,
                                                   parent_name,
                                                   new_auth,
                                                   &new_private_data,
                                                   delegate);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error changing object authorization data: "
               << GetErrorString(result);
    return result;
  }
  if (key_blob) {
    TPMT_PUBLIC public_data;
    result = GetKeyPublicArea(key_handle, &public_data);
    if (result != TPM_RC_SUCCESS) {
      return result;
    }
    result = KeyDataToString(Make_TPM2B_PUBLIC(public_data),
                             new_private_data,
                             key_blob);
    if (result != TPM_RC_SUCCESS) {
      return result;
    }
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::ImportRSAKey(AsymmetricKeyUsage key_type,
                                    const std::string& modulus,
                                    uint32_t public_exponent,
                                    const std::string& prime_factor,
                                    const std::string& password,
                                    AuthorizationDelegate* delegate,
                                    std::string* key_blob) {
  TPM_RC result;
  if (delegate == nullptr) {
    result = SAPI_RC_INVALID_SESSIONS;
    LOG(ERROR) << "This method needs a valid authorization delegate: "
               << GetErrorString(result);
    return result;
  }
  std::string parent_name;
  result = GetKeyName(kRSAStorageRootKey, &parent_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting Key name for RSA-SRK: "
               << GetErrorString(result);
    return result;
  }
  TPMT_PUBLIC public_area = CreateDefaultPublicArea(TPM_ALG_RSA);
  public_area.object_attributes = kUserWithAuth | kNoDA;
  switch (key_type) {
    case AsymmetricKeyUsage::kDecryptKey:
      public_area.object_attributes |= kDecrypt;
      break;
    case AsymmetricKeyUsage::kSignKey:
      public_area.object_attributes |= kSign;
      break;
    case AsymmetricKeyUsage::kDecryptAndSignKey:
      public_area.object_attributes |= (kSign | kDecrypt);
      break;
  }
  public_area.parameters.rsa_detail.key_bits = modulus.size() * 8;
  public_area.parameters.rsa_detail.exponent = public_exponent;
  public_area.unique.rsa = Make_TPM2B_PUBLIC_KEY_RSA(modulus);
  TPM2B_DATA encryption_key;
  encryption_key.size = kAesKeySize;
  CHECK_EQ(RAND_bytes(encryption_key.buffer, encryption_key.size), 1) <<
      "Error generating a cryptographically random Aes Key.";
  TPM2B_PUBLIC public_data = Make_TPM2B_PUBLIC(public_area);
  TPM2B_ENCRYPTED_SECRET in_sym_seed = Make_TPM2B_ENCRYPTED_SECRET("");
  TPMT_SYM_DEF_OBJECT symmetric_alg;
  symmetric_alg.algorithm = TPM_ALG_AES;
  symmetric_alg.key_bits.aes = kAesKeySize * 8;
  symmetric_alg.mode.aes = TPM_ALG_CFB;
  TPMT_SENSITIVE in_sensitive;
  in_sensitive.sensitive_type = TPM_ALG_RSA;
  in_sensitive.auth_value = Make_TPM2B_DIGEST(password);
  in_sensitive.seed_value = Make_TPM2B_DIGEST("");
  in_sensitive.sensitive.rsa = Make_TPM2B_PRIVATE_KEY_RSA(prime_factor);
  TPM2B_PRIVATE private_data;
  result = EncryptPrivateData(in_sensitive, public_area,
                              &private_data, &encryption_key);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error creating encrypted private struct: "
               << GetErrorString(result);
    return result;
  }
  TPM2B_PRIVATE tpm_private_data;
  tpm_private_data.size = 0;
  result = factory_.GetTpm()->ImportSync(kRSAStorageRootKey,
                                         parent_name,
                                         encryption_key,
                                         public_data,
                                         private_data,
                                         in_sym_seed,
                                         symmetric_alg,
                                         &tpm_private_data,
                                         delegate);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error importing key: " << GetErrorString(result);
    return result;
  }
  if (key_blob) {
    result = KeyDataToString(public_data, tpm_private_data, key_blob);
    if (result != TPM_RC_SUCCESS) {
      return result;
    }
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::CreateRSAKeyPair(AsymmetricKeyUsage key_type,
                                        int modulus_bits,
                                        uint32_t public_exponent,
                                        const std::string& password,
                                        const std::string& policy_digest,
                                        bool use_only_policy_authorization,
                                        AuthorizationDelegate* delegate,
                                        std::string* key_blob,
                                        std::string* creation_blob) {
  CHECK(key_blob);
  TPM_RC result;
  if (delegate == nullptr) {
    result = SAPI_RC_INVALID_SESSIONS;
    LOG(ERROR) << "This method needs a valid authorization delegate: "
               << GetErrorString(result);
    return result;
  }
  std::string parent_name;
  result = GetKeyName(kRSAStorageRootKey, &parent_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting Key name for RSA-SRK: "
               << GetErrorString(result);
    return result;
  }
  TPMT_PUBLIC public_area = CreateDefaultPublicArea(TPM_ALG_RSA);
  public_area.auth_policy = Make_TPM2B_DIGEST(policy_digest);
  public_area.object_attributes |=
      (kSensitiveDataOrigin | kUserWithAuth | kNoDA);
  switch (key_type) {
    case AsymmetricKeyUsage::kDecryptKey:
      public_area.object_attributes |= kDecrypt;
      break;
    case AsymmetricKeyUsage::kSignKey:
      public_area.object_attributes |= kSign;
      break;
    case AsymmetricKeyUsage::kDecryptAndSignKey:
      public_area.object_attributes |= (kSign | kDecrypt);
      break;
  }
  if (use_only_policy_authorization && !policy_digest.empty()) {
    public_area.object_attributes |= kAdminWithPolicy;
    public_area.object_attributes &= (~kUserWithAuth);
  }
  public_area.parameters.rsa_detail.key_bits = modulus_bits;
  public_area.parameters.rsa_detail.exponent = public_exponent;
  TPML_PCR_SELECTION creation_pcrs;
  creation_pcrs.count = 0;
  TPMS_SENSITIVE_CREATE sensitive;
  sensitive.user_auth = Make_TPM2B_DIGEST(password);
  sensitive.data = Make_TPM2B_SENSITIVE_DATA("");
  TPM2B_SENSITIVE_CREATE sensitive_create = Make_TPM2B_SENSITIVE_CREATE(
      sensitive);
  TPM2B_DATA outside_info = Make_TPM2B_DATA("");
  TPM2B_PUBLIC out_public;
  out_public.size = 0;
  TPM2B_PRIVATE out_private;
  out_private.size = 0;
  TPM2B_CREATION_DATA creation_data;
  TPM2B_DIGEST creation_hash;
  TPMT_TK_CREATION creation_ticket;
  result = factory_.GetTpm()->CreateSync(kRSAStorageRootKey,
                                         parent_name,
                                         sensitive_create,
                                         Make_TPM2B_PUBLIC(public_area),
                                         outside_info,
                                         creation_pcrs,
                                         &out_private,
                                         &out_public,
                                         &creation_data,
                                         &creation_hash,
                                         &creation_ticket,
                                         delegate);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error creating RSA key: " << GetErrorString(result);
    return result;
  }
  result = KeyDataToString(out_public, out_private, key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing key_blob: " << GetErrorString(result);
    return result;
  }
  if (creation_blob) {
    result = Serialize_TPM2B_CREATION_DATA(creation_data, creation_blob);
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << "Error serializing creation data struct: "
                 << GetErrorString(result);
      return result;
    }
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::LoadKey(const std::string& key_blob,
                               AuthorizationDelegate* delegate,
                               TPM_HANDLE* key_handle) {
  TPM_RC result;
  if (delegate == nullptr) {
    result = SAPI_RC_INVALID_SESSIONS;
    LOG(ERROR) << "This method needs a valid authorization delegate: "
               << GetErrorString(result);
    return result;
  }
  std::string parent_name;
  result = GetKeyName(kRSAStorageRootKey, &parent_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting parent key name: " << GetErrorString(result);
    return result;
  }
  TPM2B_PUBLIC in_public;
  TPM2B_PRIVATE in_private;
  result = StringToKeyData(key_blob, &in_public, &in_private);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error parsing key_blob: " << GetErrorString(result);
    return result;
  }
  CHECK(key_handle);
  TPM2B_NAME key_name;
  key_name.size = 0;
  result = factory_.GetTpm()->LoadSync(kRSAStorageRootKey,
                                       parent_name,
                                       in_private,
                                       in_public,
                                       key_handle,
                                       &key_name,
                                       delegate);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error loading key: " << GetErrorString(result);
    return result;
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::GetKeyName(TPM_HANDLE handle, std::string* name) {
  CHECK(name);
  TPM_RC result;
  TPMT_PUBLIC public_data;
  result = GetKeyPublicArea(handle, &public_data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error fetching public info: " << GetErrorString(result);
    return result;
  }
  result = ComputeKeyName(public_data, name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error computing key name: " << GetErrorString(result);
    return TPM_RC_SUCCESS;
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::GetKeyPublicArea(TPM_HANDLE handle,
                                        TPMT_PUBLIC* public_data) {
  CHECK(public_data);
  TPM2B_NAME out_name;
  TPM2B_PUBLIC public_area;
  TPM2B_NAME qualified_name;
  std::string handle_name;  // Unused
  TPM_RC return_code = factory_.GetTpm()->ReadPublicSync(handle,
                                                         handle_name,
                                                         &public_area,
                                                         &out_name,
                                                         &qualified_name,
                                                         nullptr);
  if (return_code) {
    LOG(ERROR) << "Error getting public area for object: " << handle;
    return return_code;
  }
  *public_data = public_area.public_area;
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::DefineNVSpace(uint32_t index,
                                     size_t num_bytes,
                                     AuthorizationDelegate* delegate) {
  TPM_RC result;
  if (num_bytes > MAX_NV_INDEX_SIZE) {
    result = SAPI_RC_BAD_SIZE;
    LOG(ERROR) << "Cannot define non-volatile space of given size: "
               << GetErrorString(result);
    return result;
  }
  if (index > kMaxNVSpaceIndex) {
    result = SAPI_RC_BAD_PARAMETER;
    LOG(ERROR) << "Cannot define non-volatile space with the given index: "
               << GetErrorString(result);
    return result;
  }
  if (delegate == nullptr) {
    result = SAPI_RC_INVALID_SESSIONS;
    LOG(ERROR) << "This method needs a valid authorization delegate: "
               << GetErrorString(result);
    return result;
  }
  uint32_t nv_index = NV_INDEX_FIRST + index;
  TPMS_NV_PUBLIC public_data;
  public_data.nv_index = nv_index;
  public_data.name_alg = TPM_ALG_SHA256;
  public_data.attributes = TPMA_NV_OWNERWRITE |
                           TPMA_NV_WRITEDEFINE |
                           TPMA_NV_AUTHREAD;
  public_data.auth_policy = Make_TPM2B_DIGEST("");
  public_data.data_size = num_bytes;
  TPM2B_AUTH authorization = Make_TPM2B_DIGEST("");
  TPM2B_NV_PUBLIC public_area = Make_TPM2B_NV_PUBLIC(public_data);
  result = factory_.GetTpm()->NV_DefineSpaceSync(
      TPM_RH_OWNER,
      NameFromHandle(TPM_RH_OWNER),
      authorization,
      public_area,
      delegate);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error defining non-volatile space: "
               << GetErrorString(result);
    return result;
  }
  nvram_public_area_map_[index] = public_data;
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::DestroyNVSpace(uint32_t index,
                                      AuthorizationDelegate* delegate) {
  TPM_RC result;
  if (index > kMaxNVSpaceIndex) {
    result = SAPI_RC_BAD_PARAMETER;
    LOG(ERROR) << "Cannot undefine non-volatile space with the given index: "
               << GetErrorString(result);
    return result;
  }
  if (delegate == nullptr) {
    result = SAPI_RC_INVALID_SESSIONS;
    LOG(ERROR) << "This method needs a valid authorization delegate: "
               << GetErrorString(result);
    return result;
  }
  std::string nv_name;
  result = GetNVSpaceName(index, &nv_name);
  if (result != TPM_RC_SUCCESS) {
    return result;
  }
  uint32_t nv_index = NV_INDEX_FIRST + index;
  result = factory_.GetTpm()->NV_UndefineSpaceSync(
      TPM_RH_OWNER,
      NameFromHandle(TPM_RH_OWNER),
      nv_index,
      nv_name,
      delegate);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error undefining non-volatile space: "
               << GetErrorString(result);
    return result;
  }
  nvram_public_area_map_.erase(index);
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::LockNVSpace(uint32_t index,
                                   AuthorizationDelegate* delegate) {
  TPM_RC result;
  if (index > kMaxNVSpaceIndex) {
    result = SAPI_RC_BAD_PARAMETER;
    LOG(ERROR) << "Cannot lock non-volatile space with the given index: "
               << GetErrorString(result);
    return result;
  }
  if (delegate == nullptr) {
    result = SAPI_RC_INVALID_SESSIONS;
    LOG(ERROR) << "This method needs a valid authorization delegate: "
               << GetErrorString(result);
    return result;
  }
  std::string nv_name;
  result = GetNVSpaceName(index, &nv_name);
  if (result != TPM_RC_SUCCESS) {
    return result;
  }
  uint32_t nv_index = NV_INDEX_FIRST + index;
  result = factory_.GetTpm()->NV_WriteLockSync(nv_index,
                                               nv_name,
                                               nv_index,
                                               nv_name,
                                               delegate);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error locking non-volatile spaces: "
               << GetErrorString(result);
    return result;
  }
  auto it = nvram_public_area_map_.find(index);
  if (it != nvram_public_area_map_.end()) {
    it->second.attributes |= TPMA_NV_WRITELOCKED;
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::WriteNVSpace(uint32_t index,
                                    uint32_t offset,
                                    const std::string& nvram_data,
                                    AuthorizationDelegate* delegate) {
  TPM_RC result;
  if (nvram_data.size() > MAX_NV_BUFFER_SIZE) {
    result = SAPI_RC_BAD_SIZE;
    LOG(ERROR) << "Insufficient buffer for non-volatile write: "
               << GetErrorString(result);
    return result;
  }
  if (index > kMaxNVSpaceIndex) {
    result = SAPI_RC_BAD_PARAMETER;
    LOG(ERROR) << "Cannot write to non-volatile space with the given index: "
               << GetErrorString(result);
    return result;
  }
  if (delegate == nullptr) {
    result = SAPI_RC_INVALID_SESSIONS;
    LOG(ERROR) << "This method needs a valid authorization delegate: "
               << GetErrorString(result);
    return result;
  }
  std::string nv_name;
  result = GetNVSpaceName(index, &nv_name);
  if (result != TPM_RC_SUCCESS) {
    return result;
  }
  uint32_t nv_index = NV_INDEX_FIRST + index;
  result = factory_.GetTpm()->NV_WriteSync(TPM_RH_OWNER,
                                           NameFromHandle(TPM_RH_OWNER),
                                           nv_index,
                                           nv_name,
                                           Make_TPM2B_MAX_NV_BUFFER(nvram_data),
                                           offset,
                                           delegate);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error writing to non-volatile space: "
               << GetErrorString(result);
    return result;
  }
  auto it = nvram_public_area_map_.find(index);
  if (it != nvram_public_area_map_.end()) {
    it->second.attributes |= TPMA_NV_WRITTEN;
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::ReadNVSpace(uint32_t index,
                                   uint32_t offset,
                                   size_t num_bytes,
                                   std::string* nvram_data,
                                   AuthorizationDelegate* delegate) {
  TPM_RC result;
  if (num_bytes > MAX_NV_BUFFER_SIZE) {
    result = SAPI_RC_BAD_SIZE;
    LOG(ERROR) << "Insufficient buffer for non-volatile read: "
               << GetErrorString(result);
    return result;
  }
  if (index > kMaxNVSpaceIndex) {
    result = SAPI_RC_BAD_PARAMETER;
    LOG(ERROR) << "Cannot read from non-volatile space with the given index: "
               << GetErrorString(result);
    return result;
  }
  if (delegate == nullptr) {
    result = SAPI_RC_INVALID_SESSIONS;
    LOG(ERROR) << "This method needs a valid authorization delegate: "
               << GetErrorString(result);
    return result;
  }
  std::string nv_name;
  result = GetNVSpaceName(index, &nv_name);
  if (result != TPM_RC_SUCCESS) {
    return result;
  }
  uint32_t nv_index = NV_INDEX_FIRST + index;
  TPM2B_MAX_NV_BUFFER data_buffer;
  data_buffer.size = 0;
  result = factory_.GetTpm()->NV_ReadSync(nv_index,
                                          nv_name,
                                          nv_index,
                                          nv_name,
                                          num_bytes,
                                          offset,
                                          &data_buffer,
                                          delegate);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error reading from non-volatile space: "
               << GetErrorString(result);
    return result;
  }
  nvram_data->assign(StringFrom_TPM2B_MAX_NV_BUFFER(data_buffer));
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::GetNVSpaceName(uint32_t index, std::string* name) {
  TPM_RC result;
  if (index > kMaxNVSpaceIndex) {
    result = SAPI_RC_BAD_PARAMETER;
    LOG(ERROR) << "Cannot read from non-volatile space with the given index: "
               << GetErrorString(result);
    return result;
  }
  TPMS_NV_PUBLIC nv_public_data;
  result = GetNVSpacePublicArea(index, &nv_public_data);
  if (result != TPM_RC_SUCCESS) {
    return result;
  }
  result = ComputeNVSpaceName(nv_public_data, name);
  if (result != TPM_RC_SUCCESS) {
    return result;
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::GetNVSpacePublicArea(uint32_t index,
                                            TPMS_NV_PUBLIC* public_data) {
  TPM_RC result;
  if (index > kMaxNVSpaceIndex) {
    result = SAPI_RC_BAD_PARAMETER;
    LOG(ERROR) << "Cannot read from non-volatile space with the given index: "
               << GetErrorString(result);
    return result;
  }
  auto it = nvram_public_area_map_.find(index);
  if (it != nvram_public_area_map_.end()) {
    *public_data = it->second;
    return TPM_RC_SUCCESS;
  }
  TPM2B_NAME nvram_name;
  TPM2B_NV_PUBLIC public_area;
  public_area.nv_public.nv_index = 0;
  uint32_t nv_index = NV_INDEX_FIRST + index;
  result = factory_.GetTpm()->NV_ReadPublicSync(nv_index,
                                                "",
                                                &public_area,
                                                &nvram_name,
                                                nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error reading non-volatile space public information: "
               << GetErrorString(result);
    return result;
  }
  *public_data = public_area.nv_public;
  nvram_public_area_map_[index] = public_area.nv_public;
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::SetKnownOwnerPassword(
    const std::string& known_owner_password) {
  scoped_ptr<TpmState> tpm_state(factory_.GetTpmState());
  TPM_RC result = tpm_state->Initialize();
  if (result) {
    LOG(ERROR) << __func__ << ": " << GetErrorString(result);
    return result;
  }
  scoped_ptr<AuthorizationDelegate> delegate =
      factory_.GetPasswordAuthorization("");
  if (tpm_state->IsOwnerPasswordSet()) {
    LOG(INFO) << "Owner password is already set. "
              << "This is normal if ownership is already taken.";
    return TPM_RC_SUCCESS;
  }
  result = SetHierarchyAuthorization(TPM_RH_OWNER,
                                     known_owner_password,
                                     delegate.get());
  if (result) {
    LOG(ERROR) << "Error setting storage hierarchy authorization "
               << "to its default value: " << GetErrorString(result);
    return result;
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::CreateStorageRootKeys(
    const std::string& owner_password) {
  TPM_RC result = TPM_RC_SUCCESS;
  scoped_ptr<TpmState>  tpm_state(factory_.GetTpmState());
  result = tpm_state->Initialize();
  if (result) {
    LOG(ERROR) << __func__ << ": " << GetErrorString(result);
    return result;
  }
  Tpm* tpm = factory_.GetTpm();
  TPMT_PUBLIC public_area;
  TPML_PCR_SELECTION creation_pcrs;
  creation_pcrs.count = 0;
  TPMS_SENSITIVE_CREATE sensitive;
  sensitive.user_auth = Make_TPM2B_DIGEST("");
  sensitive.data = Make_TPM2B_SENSITIVE_DATA("");
  TPM_HANDLE object_handle;
  TPM2B_CREATION_DATA creation_data;
  TPM2B_DIGEST creation_digest;
  TPMT_TK_CREATION creation_ticket;
  TPM2B_NAME object_name;
  object_name.size = 0;
  scoped_ptr<AuthorizationDelegate> delegate =
      factory_.GetPasswordAuthorization(owner_password);
  if ((tpm_state->IsRSASupported()) &&
      (GetKeyPublicArea(kRSAStorageRootKey, &public_area) != TPM_RC_SUCCESS)) {
    public_area = CreateDefaultPublicArea(TPM_ALG_RSA);
    public_area.object_attributes |=
        (kSensitiveDataOrigin | kUserWithAuth | kNoDA |
         kRestricted | kDecrypt);
    public_area.parameters.rsa_detail.symmetric.algorithm = TPM_ALG_AES;
    public_area.parameters.rsa_detail.symmetric.key_bits.aes = 128;
    public_area.parameters.rsa_detail.symmetric.mode.aes = TPM_ALG_CFB;
    TPM2B_PUBLIC rsa_public_area = Make_TPM2B_PUBLIC(public_area);
    result = tpm->CreatePrimarySync(TPM_RH_OWNER,
                                    NameFromHandle(TPM_RH_OWNER),
                                    Make_TPM2B_SENSITIVE_CREATE(sensitive),
                                    rsa_public_area,
                                    Make_TPM2B_DATA(""),
                                    creation_pcrs,
                                    &object_handle,
                                    &rsa_public_area,
                                    &creation_data,
                                    &creation_digest,
                                    &creation_ticket,
                                    &object_name,
                                    delegate.get());
    if (result) {
      LOG(ERROR) << __func__ << ": " << GetErrorString(result);
      return result;
    }
    ScopedKeyHandle rsa_key(factory_, object_handle);
    // This will make the key persistent.
    result = tpm->EvictControlSync(TPM_RH_OWNER,
                                   NameFromHandle(TPM_RH_OWNER),
                                   object_handle,
                                   StringFrom_TPM2B_NAME(object_name),
                                   kRSAStorageRootKey,
                                   delegate.get());
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << __func__ << ": " << GetErrorString(result);
      return result;
    }
  } else {
    LOG(INFO) << "Not creating RSA SRK because it isnt supported or it exists.";
  }

  // Do it again for ECC.
  if ((tpm_state->IsECCSupported()) &&
      (GetKeyPublicArea(kECCStorageRootKey, &public_area) != TPM_RC_SUCCESS)) {
    public_area = CreateDefaultPublicArea(TPM_ALG_ECC);
    public_area.object_attributes |=
        (kSensitiveDataOrigin | kUserWithAuth | kNoDA |
         kRestricted | kDecrypt);
    public_area.parameters.ecc_detail.symmetric.algorithm = TPM_ALG_AES;
    public_area.parameters.ecc_detail.symmetric.key_bits.aes = 128;
    public_area.parameters.ecc_detail.symmetric.mode.aes = TPM_ALG_CFB;
    TPM2B_PUBLIC ecc_public_area = Make_TPM2B_PUBLIC(public_area);
    result = tpm->CreatePrimarySync(TPM_RH_OWNER,
                                    NameFromHandle(TPM_RH_OWNER),
                                    Make_TPM2B_SENSITIVE_CREATE(sensitive),
                                    ecc_public_area,
                                    Make_TPM2B_DATA(""),
                                    creation_pcrs,
                                    &object_handle,
                                    &ecc_public_area,
                                    &creation_data,
                                    &creation_digest,
                                    &creation_ticket,
                                    &object_name,
                                    delegate.get());
    if (result) {
      LOG(ERROR) << __func__ << ": " << GetErrorString(result);
      return result;
    }
    ScopedKeyHandle ecc_key(factory_, object_handle);
    // This will make the key persistent.
    result = tpm->EvictControlSync(TPM_RH_OWNER,
                                   NameFromHandle(TPM_RH_OWNER),
                                   object_handle,
                                   StringFrom_TPM2B_NAME(object_name),
                                   kECCStorageRootKey,
                                   delegate.get());
    if (result != TPM_RC_SUCCESS) {
      LOG(ERROR) << __func__ << ": " << GetErrorString(result);
      return result;
    }
  } else {
    LOG(INFO) << "Not creating ECC SRK because it isnt supported or it exists.";
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::CreateSaltingKey(const std::string& owner_password) {
  TPMT_PUBLIC public_area;
  TPM_RC result = GetKeyPublicArea(kSaltingKey, &public_area);
  if (result == TPM_RC_SUCCESS) {
    LOG(INFO) << "Salting key already exists.";
    return result;
  }
  std::string parent_name;
  result = GetKeyName(kRSAStorageRootKey, &parent_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error getting Key name for RSA-SRK: "
               << GetErrorString(result);
    return result;
  }
  public_area = CreateDefaultPublicArea(TPM_ALG_RSA);
  public_area.name_alg = TPM_ALG_SHA256;
  public_area.object_attributes |=
      kSensitiveDataOrigin | kUserWithAuth | kNoDA | kDecrypt;
  TPML_PCR_SELECTION creation_pcrs;
  creation_pcrs.count = 0;
  TPMS_SENSITIVE_CREATE sensitive;
  sensitive.user_auth = Make_TPM2B_DIGEST("");
  sensitive.data = Make_TPM2B_SENSITIVE_DATA("");
  TPM2B_SENSITIVE_CREATE sensitive_create = Make_TPM2B_SENSITIVE_CREATE(
      sensitive);
  TPM2B_DATA outside_info = Make_TPM2B_DATA("");

  TPM2B_PRIVATE out_private;
  out_private.size = 0;
  TPM2B_PUBLIC out_public;
  out_public.size = 0;
  TPM2B_CREATION_DATA creation_data;
  TPM2B_DIGEST creation_hash;
  TPMT_TK_CREATION creation_ticket;
  // TODO(usanghi): MITM vulnerability with SaltingKey creation.
  // Currently we cannot verify the key returned by the TPM.
  // crbug.com/442331
  scoped_ptr<AuthorizationDelegate> delegate =
      factory_.GetPasswordAuthorization("");
  result = factory_.GetTpm()->CreateSync(kRSAStorageRootKey,
                                         parent_name,
                                         sensitive_create,
                                         Make_TPM2B_PUBLIC(public_area),
                                         outside_info,
                                         creation_pcrs,
                                         &out_private,
                                         &out_public,
                                         &creation_data,
                                         &creation_hash,
                                         &creation_ticket,
                                         delegate.get());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error creating salting key: " << GetErrorString(result);
    return result;
  }
  TPM2B_NAME key_name;
  key_name.size = 0;
  TPM_HANDLE key_handle;
  result = factory_.GetTpm()->LoadSync(kRSAStorageRootKey,
                                       parent_name,
                                       out_private,
                                       out_public,
                                       &key_handle,
                                       &key_name,
                                       delegate.get());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error loading salting key: " << GetErrorString(result);
    return result;
  }
  ScopedKeyHandle key(factory_, key_handle);
  scoped_ptr<AuthorizationDelegate> owner_delegate =
      factory_.GetPasswordAuthorization(owner_password);
  result = factory_.GetTpm()->EvictControlSync(TPM_RH_OWNER,
                                               NameFromHandle(TPM_RH_OWNER),
                                               key_handle,
                                               StringFrom_TPM2B_NAME(key_name),
                                               kSaltingKey,
                                               owner_delegate.get());
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << __func__ << ": " << GetErrorString(result);
    return result;
  }
  return TPM_RC_SUCCESS;
}

TPMT_PUBLIC TpmUtilityImpl::CreateDefaultPublicArea(TPM_ALG_ID key_alg) {
  TPMT_PUBLIC public_area;
  public_area.name_alg = TPM_ALG_SHA256;
  public_area.auth_policy = Make_TPM2B_DIGEST("");
  public_area.object_attributes = kFixedTPM | kFixedParent;
  if (key_alg == TPM_ALG_RSA) {
    public_area.type = TPM_ALG_RSA;
    public_area.parameters.rsa_detail.scheme.scheme = TPM_ALG_NULL;
    public_area.parameters.rsa_detail.symmetric.algorithm = TPM_ALG_NULL;
    public_area.parameters.rsa_detail.key_bits = 2048;
    public_area.parameters.rsa_detail.exponent = 0;
    public_area.unique.rsa = Make_TPM2B_PUBLIC_KEY_RSA("");
  } else if (key_alg == TPM_ALG_ECC) {
    public_area.type = TPM_ALG_ECC;
    public_area.parameters.ecc_detail.curve_id = TPM_ECC_NIST_P256;
    public_area.parameters.ecc_detail.kdf.scheme = TPM_ALG_MGF1;
    public_area.parameters.ecc_detail.kdf.details.mgf1.hash_alg =
        TPM_ALG_SHA256;
    public_area.unique.ecc.x = Make_TPM2B_ECC_PARAMETER("");
    public_area.unique.ecc.y = Make_TPM2B_ECC_PARAMETER("");
  } else {
    LOG(WARNING) << "Unrecognized key_type. Not filling parameters.";
  }
  return public_area;
}

TPM_RC TpmUtilityImpl::SetHierarchyAuthorization(
    TPMI_RH_HIERARCHY_AUTH hierarchy,
    const std::string& password,
    AuthorizationDelegate* authorization) {
  if (password.size() > kMaxPasswordLength) {
    LOG(ERROR) << "Hierarchy passwords can be at most " << kMaxPasswordLength
               << " bytes. Current password length is: " << password.size();
    return SAPI_RC_BAD_SIZE;
  }
  return factory_.GetTpm()->HierarchyChangeAuthSync(
      hierarchy,
      NameFromHandle(hierarchy),
      Make_TPM2B_DIGEST(password),
      authorization);
}

TPM_RC TpmUtilityImpl::DisablePlatformHierarchy(
    AuthorizationDelegate* authorization) {
  return factory_.GetTpm()->HierarchyControlSync(
      TPM_RH_PLATFORM,  // The authorizing entity.
      NameFromHandle(TPM_RH_PLATFORM),
      TPM_RH_PLATFORM,  // The target hierarchy.
      0,  // Disable.
      authorization);
}

TPM_RC TpmUtilityImpl::StringToKeyData(const std::string& key_blob,
                                       TPM2B_PUBLIC* public_info,
                                       TPM2B_PRIVATE* private_info) {
  if (!public_info || !private_info) {
    LOG(WARNING) << "Output arguments not defined.";
    return TPM_RC_SUCCESS;
  }
  if (key_blob.size() == 0) {
    public_info->size = 0;
    private_info->size = 0;
    return TPM_RC_SUCCESS;
  }
  std::string mutable_key_blob = key_blob;
  TPM_RC result = Parse_TPM2B_PUBLIC(&mutable_key_blob, public_info, nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error parsing TPM2B_Public: " << GetErrorString(result);
    return result;
  }
  result = Parse_TPM2B_PRIVATE(&mutable_key_blob, private_info, nullptr);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error parsing TPM2B_Private: " << GetErrorString(result);
    return result;
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::KeyDataToString(const TPM2B_PUBLIC& public_info,
                                       const TPM2B_PRIVATE& private_info,
                                       std::string* key_blob) {
  if (!key_blob) {
    LOG(WARNING) << "Output arguments not defined.";
    return TPM_RC_SUCCESS;
  }
  key_blob->clear();
  if ((public_info.size == 0) && (private_info.size == 0)) {
    return TPM_RC_SUCCESS;
  }
  TPM_RC result = Serialize_TPM2B_PUBLIC(public_info, key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing TPM2B_Public: " << GetErrorString(result);
    return result;
  }
  result = Serialize_TPM2B_PRIVATE(private_info, key_blob);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing TPM2B_Private: "
               << GetErrorString(result);
    return result;
  }
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::ComputeKeyName(const TPMT_PUBLIC& public_area,
                                      std::string* object_name) {
  CHECK(object_name);
  if (public_area.type == TPM_ALG_ERROR) {
    // We do not compute a name for empty public area.
    object_name->clear();
    return TPM_RC_SUCCESS;
  }
  std::string serialized_public_area;
  TPM_RC result = Serialize_TPMT_PUBLIC(public_area, &serialized_public_area);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing public area: " << GetErrorString(result);
    return result;
  }
  std::string serialized_name_alg;
  result = Serialize_TPM_ALG_ID(TPM_ALG_SHA256, &serialized_name_alg);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing public area: " << GetErrorString(result);
    return result;
  }
  object_name->assign(serialized_name_alg +
                      crypto::SHA256HashString(serialized_public_area));
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::ComputeNVSpaceName(const TPMS_NV_PUBLIC& nv_public_area,
                                          std::string* nv_name) {
  CHECK(nv_name);
  if ((nv_public_area.nv_index & NV_INDEX_FIRST) == 0) {
    // If the index is not an nvram index, we do not compute a name.
    nv_name->clear();
    return TPM_RC_SUCCESS;
  }
  std::string serialized_public_area;
  TPM_RC result = Serialize_TPMS_NV_PUBLIC(nv_public_area,
                                           &serialized_public_area);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing public area: " << GetErrorString(result);
    return result;
  }
  std::string serialized_name_alg;
  result = Serialize_TPM_ALG_ID(TPM_ALG_SHA256, &serialized_name_alg);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing public area: " << GetErrorString(result);
    return result;
  }
  nv_name->assign(serialized_name_alg +
                  crypto::SHA256HashString(serialized_public_area));
  return TPM_RC_SUCCESS;
}

TPM_RC TpmUtilityImpl::EncryptPrivateData(const TPMT_SENSITIVE& sensitive_area,
                                          const TPMT_PUBLIC& public_area,
                                          TPM2B_PRIVATE* encrypted_private_data,
                                          TPM2B_DATA* encryption_key) {
  TPM2B_SENSITIVE sensitive_data = Make_TPM2B_SENSITIVE(sensitive_area);
  std::string serialized_sensitive_data;
  TPM_RC result = Serialize_TPM2B_SENSITIVE(sensitive_data,
                                            &serialized_sensitive_data);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing sensitive data: "
               << GetErrorString(result);
    return result;
  }
  std::string object_name;
  result = ComputeKeyName(public_area, &object_name);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error computing object name: " << GetErrorString(result);
    return result;
  }
  TPM2B_DIGEST inner_integrity = Make_TPM2B_DIGEST(crypto::SHA256HashString(
      serialized_sensitive_data + object_name));
  std::string serialized_inner_integrity;
  result = Serialize_TPM2B_DIGEST(inner_integrity, &serialized_inner_integrity);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error serializing inner integrity: "
               << GetErrorString(result);
    return result;
  }
  std::string unencrypted_private_data = serialized_inner_integrity +
                                         serialized_sensitive_data;
  AES_KEY key;
  AES_set_encrypt_key(encryption_key->buffer, kAesKeySize * 8, &key);
  std::string private_data_string(unencrypted_private_data.size(), 0);
  int iv_in = 0;
  unsigned char iv[MAX_AES_BLOCK_SIZE_BYTES] = {0};
  AES_cfb128_encrypt(
    reinterpret_cast<const unsigned char*>(unencrypted_private_data.data()),
    reinterpret_cast<unsigned char*>(string_as_array(&private_data_string)),
    unencrypted_private_data.size(), &key, iv, &iv_in, AES_ENCRYPT);
  *encrypted_private_data = Make_TPM2B_PRIVATE(private_data_string);
  if (result != TPM_RC_SUCCESS) {
    LOG(ERROR) << "Error making private area: "
               << GetErrorString(result);
    return result;
  }
  return TPM_RC_SUCCESS;
}

}  // namespace trunks
