// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/backend.h"

#include <functional>
#include <string>

#include <base/callback_helpers.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "libhwsec/error/tpm1_error.h"
#include "libhwsec/overalls/overalls.h"
#include "libhwsec/status.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using hwsec_foundation::status::MakeStatus;

namespace hwsec {

using SealingTpm1 = BackendTpm1::SealingTpm1;

StatusOr<ScopedTssKey> SealingTpm1::GetAuthValueKey(
    const brillo::SecureBlob& auth_value) {
  ASSIGN_OR_RETURN(const TssTpmContext& user_context,
                   backend_.GetTssUserContext());

  overalls::Overalls& overalls = backend_.overall_context_.overalls;

  ScopedTssKey enc_handle(overalls, user_context.context);

  // Create the enc_handle.
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Context_CreateObject(
                      user_context.context, TSS_OBJECT_TYPE_ENCDATA,
                      TSS_ENCDATA_SEAL, enc_handle.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_Context_CreateObject");

  // Get the TPM usage policy object and set the auth_value.
  TSS_HPOLICY tpm_usage_policy;
  RETURN_IF_ERROR(
      MakeStatus<TPM1Error>(overalls.Ospi_GetPolicyObject(
          user_context.tpm_handle, TSS_POLICY_USAGE, &tpm_usage_policy)))
      .WithStatus<TPMError>("Failed to call Ospi_GetPolicyObject");

  brillo::SecureBlob mutable_auth_value = auth_value;
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Policy_SetSecret(
                      tpm_usage_policy, TSS_SECRET_MODE_PLAIN,
                      mutable_auth_value.size(), mutable_auth_value.data())))
      .WithStatus<TPMError>("Failed to call Ospi_Policy_SetSecret");

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Policy_AssignToObject(
                      tpm_usage_policy, enc_handle)))
      .WithStatus<TPMError>("Failed to call Ospi_Policy_AssignToObject");

  return enc_handle;
}

StatusOr<brillo::Blob> SealingTpm1::Seal(
    const OperationPolicySetting& policy,
    const brillo::SecureBlob& unsealed_data) {
  ASSIGN_OR_RETURN(
      ScopedKey srk,
      backend_.key_managerment_.GetPersistentKey(
          Backend::KeyManagerment::PersistentKeyType::kStorageRootKey));

  ASSIGN_OR_RETURN(const KeyTpm1& srk_data,
                   backend_.key_managerment_.GetKeyData(srk.GetKey()));

  ASSIGN_OR_RETURN(const TssTpmContext& user_context,
                   backend_.GetTssUserContext());

  overalls::Overalls& overalls = backend_.overall_context_.overalls;

  ASSIGN_OR_RETURN(
      const ConfigTpm1::PcrMap& settings,
      backend_.config_.ToSettingsPcrMap(policy.device_config_settings),
      _.WithStatus<TPMError>("Failed to convert setting to PCR map"));

  // Create a PCRS object to hold pcr_index and pcr_value.
  ScopedTssPcrs pcrs(overalls, user_context.context);
  if (!settings.empty()) {
    RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Context_CreateObject(
                        user_context.context, TSS_OBJECT_TYPE_PCRS,
                        TSS_PCRS_STRUCT_INFO, pcrs.ptr())))
        .WithStatus<TPMError>("Failed to call Ospi_Context_CreateObject");

    for (const auto& map_pair : settings) {
      uint32_t pcr_index = map_pair.first;
      brillo::Blob pcr_value = map_pair.second;
      RETURN_IF_ERROR(
          MakeStatus<TPM1Error>(overalls.Ospi_PcrComposite_SetPcrValue(
              pcrs, pcr_index, pcr_value.size(), pcr_value.data())))
          .WithStatus<TPMError>("Failed to call Ospi_PcrComposite_SetPcrValue");
    }
  }

  if (!policy.permission.auth_value.has_value()) {
    return MakeStatus<TPMError>("Unsupported empty auth value",
                                TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(ScopedTssKey auth_value_key,
                   GetAuthValueKey(policy.permission.auth_value.value()),
                   _.WithStatus<TPMError>("Failed to get auth value key"));

  brillo::SecureBlob plaintext = unsealed_data;

  // Seal the given value with the SRK.
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Data_Seal(
                      auth_value_key, srk_data.key_handle, plaintext.size(),
                      plaintext.data(), pcrs)))
      .WithStatus<TPMError>("Failed to call Ospi_Data_Seal");

  // Extract the sealed value.
  ScopedTssMemory enc_data(overalls, user_context.context);
  uint32_t length = 0;
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_GetAttribData(
                      auth_value_key, TSS_TSPATTRIB_ENCDATA_BLOB,
                      TSS_TSPATTRIB_ENCDATABLOB_BLOB, &length, enc_data.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_GetAttribData");

  return brillo::Blob(enc_data.value(), enc_data.value() + length);
}

StatusOr<std::optional<ScopedKey>> SealingTpm1::PreloadSealedData(
    const OperationPolicy& policy, const brillo::Blob& sealed_data) {
  // TPM1.2 doesn't support repload sealed data.
  return std::nullopt;
}

StatusOr<brillo::SecureBlob> SealingTpm1::Unseal(
    const OperationPolicy& policy,
    const brillo::Blob& sealed_data,
    UnsealOptions options) {
  if (options.preload_data.has_value()) {
    return MakeStatus<TPMError>("Unsupported preload data",
                                TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(
      ScopedKey srk,
      backend_.key_managerment_.GetPersistentKey(
          Backend::KeyManagerment::PersistentKeyType::kStorageRootKey));

  ASSIGN_OR_RETURN(const KeyTpm1& srk_data,
                   backend_.key_managerment_.GetKeyData(srk.GetKey()));

  ASSIGN_OR_RETURN(const TssTpmContext& user_context,
                   backend_.GetTssUserContext());

  overalls::Overalls& overalls = backend_.overall_context_.overalls;

  if (!policy.permission.auth_value.has_value()) {
    return MakeStatus<TPMError>("Unsupported empty auth value",
                                TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(ScopedTssKey auth_value_key,
                   GetAuthValueKey(policy.permission.auth_value.value()),
                   _.WithStatus<TPMError>("Failed to get auth value key"));

  brillo::Blob mutable_sealed_data = sealed_data;
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_SetAttribData(
                      auth_value_key, TSS_TSPATTRIB_ENCDATA_BLOB,
                      TSS_TSPATTRIB_ENCDATABLOB_BLOB,
                      mutable_sealed_data.size(), mutable_sealed_data.data())))
      .WithStatus<TPMError>("Failed to call Ospi_SetAttribData");

  // Unseal using the SRK.
  ScopedTssSecureMemory dec_data(overalls, user_context.context);
  uint32_t length = 0;
  RETURN_IF_ERROR(
      MakeStatus<TPM1Error>(overalls.Ospi_Data_Unseal(
          auth_value_key, srk_data.key_handle, &length, dec_data.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_Data_Unseal");

  brillo::SecureBlob result(dec_data.value(), dec_data.value() + length);
  return result;
}

}  // namespace hwsec
