// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm1/backend.h"
#include "libhwsec/backend/tpm1/key_managerment.h"

#include <functional>
#include <string>
#include <vector>

#include <base/callback_helpers.h>
#include <brillo/secure_blob.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/secure_blob_util.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/status/status_chain_macros.h>

#include "libhwsec/error/tpm1_error.h"
#include "libhwsec/overalls/overalls.h"
#include "libhwsec/status.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using hwsec_foundation::CreateSecureRandomBlob;
using hwsec_foundation::Sha1;
using hwsec_foundation::Sha256;
using hwsec_foundation::status::MakeStatus;

namespace hwsec {

using KeyManagermentTpm1 = BackendTpm1::KeyManagermentTpm1;

namespace {

constexpr uint8_t kDefaultSrkAuth[] = {};
constexpr uint32_t kDefaultTpmRsaKeyBits = 2048;
constexpr uint32_t kDefaultDiscardableWrapPasswordLength = 32;
constexpr uint32_t kDefaultTpmRsaKeyFlag = TSS_KEY_SIZE_2048;

}  // namespace

KeyManagermentTpm1::~KeyManagermentTpm1() {
  std::vector<Key> key_list;
  for (auto& [token, data] : key_map_) {
    key_list.push_back(Key{.token = token});
  }
  for (Key key : key_list) {
    if (Status status = Flush(key); !status.ok()) {
      LOG(WARNING) << "Failed to flush key: " << status;
    }
  }
}

StatusOr<absl::flat_hash_set<KeyAlgoType>>
KeyManagermentTpm1::GetSupportedAlgo() {
  return absl::flat_hash_set<KeyAlgoType>({
      KeyAlgoType::kRsa,
  });
}

StatusOr<KeyManagermentTpm1::CreateKeyResult> KeyManagermentTpm1::CreateKey(
    const OperationPolicySetting& policy,
    KeyAlgoType key_algo,
    CreateKeyOptions options) {
  switch (key_algo) {
    case KeyAlgoType::kRsa:
      return CreateRsaKey(policy, options, /*auto_reload=*/false);
    default:
      return MakeStatus<TPMError>("Unsupported key creation algorithm",
                                  TPMRetryAction::kNoRetry);
  }
}

StatusOr<KeyManagermentTpm1::CreateKeyResult>
KeyManagermentTpm1::CreateAutoReloadKey(const OperationPolicySetting& policy,
                                        KeyAlgoType key_algo,
                                        CreateKeyOptions options) {
  switch (key_algo) {
    case KeyAlgoType::kRsa:
      return CreateRsaKey(policy, options, /*auto_reload=*/true);
    default:
      return MakeStatus<TPMError>("Unsupported key creation algorithm",
                                  TPMRetryAction::kNoRetry);
  }
}

StatusOr<KeyManagermentTpm1::CreateKeyResult> KeyManagermentTpm1::CreateRsaKey(
    const OperationPolicySetting& policy,
    const CreateKeyOptions& options,
    bool auto_reload) {
  ASSIGN_OR_RETURN(
      const ConfigTpm1::PcrMap& setting,
      backend_.config_.ToSettingsPcrMap(policy.device_config_settings),
      _.WithStatus<TPMError>("Failed to convert setting to PCR map"));

  if (policy.permission.auth_value.has_value()) {
    return MakeStatus<TPMError>("Unsupported policy permission",
                                TPMRetryAction::kNoRetry);
  }

  if (options.allow_software_gen && setting.empty()) {
    return CreateSoftwareGenRsaKey(policy, options, auto_reload);
  }

  ASSIGN_OR_RETURN(ScopedKey srk,
                   GetPersistentKey(PersistentKeyType::kStorageRootKey));

  ASSIGN_OR_RETURN(const KeyTpm1& srk_data, GetKeyData(srk.GetKey()));

  ASSIGN_OR_RETURN(const TssTpmContext& user_context,
                   backend_.GetTssUserContext());

  overalls::Overalls& overalls = backend_.overall_context_.overalls;

  // Create a PCRS object to hold pcr_index and pcr_value.
  ScopedTssPcrs pcrs(overalls, user_context.context);
  if (!setting.empty()) {
    RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Context_CreateObject(
                        user_context.context, TSS_OBJECT_TYPE_PCRS,
                        TSS_PCRS_STRUCT_INFO, pcrs.ptr())))
        .WithStatus<TPMError>("Failed to call Ospi_Context_CreateObject");

    for (const auto& map_pair : setting) {
      uint32_t pcr_index = map_pair.first;
      brillo::Blob pcr_value = map_pair.second;
      RETURN_IF_ERROR(
          MakeStatus<TPM1Error>(overalls.Ospi_PcrComposite_SetPcrValue(
              pcrs, pcr_index, pcr_value.size(), pcr_value.data())))
          .WithStatus<TPMError>("Failed to call Ospi_PcrComposite_SetPcrValue");
    }
  }

  // Create a non-migratable key restricted to |pcrs|.
  ScopedTssKey pcr_bound_key(overalls, user_context.context);
  TSS_FLAG init_flags =
      TSS_KEY_VOLATILE | TSS_KEY_NOT_MIGRATABLE | kDefaultTpmRsaKeyFlag;

  // In this case, the key is not decrypt only. It can be used to sign the
  // data too. No easy way to make a decrypt only key here.
  if (options.allow_sign && !options.allow_decrypt) {
    init_flags |= TSS_KEY_TYPE_SIGNING;
  } else {
    init_flags |= TSS_KEY_TYPE_LEGACY;
  }

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Context_CreateObject(
                      user_context.context, TSS_OBJECT_TYPE_RSAKEY, init_flags,
                      pcr_bound_key.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_Context_CreateObject");

  if (options.allow_sign) {
    uint32_t sig_scheme = TSS_SS_RSASSAPKCS1V15_DER;
    RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_SetAttribUint32(
                        pcr_bound_key, TSS_TSPATTRIB_KEY_INFO,
                        TSS_TSPATTRIB_KEYINFO_SIGSCHEME, sig_scheme)))
        .WithStatus<TPMError>("Failed to call Ospi_Context_CreateObject");
  }

  if (options.allow_decrypt) {
    uint32_t enc_scheme = TSS_ES_RSAESPKCSV15;
    RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_SetAttribUint32(
                        pcr_bound_key, TSS_TSPATTRIB_KEY_INFO,
                        TSS_TSPATTRIB_KEYINFO_ENCSCHEME, enc_scheme)))
        .WithStatus<TPMError>("Failed to call Ospi_SetAttribUint32");
  }

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Key_CreateKey(
                      pcr_bound_key, srk_data.key_handle, pcrs)))
      .WithStatus<TPMError>("Failed to call Ospi_Key_CreateKey");

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Key_LoadKey(
                      pcr_bound_key, srk_data.key_handle)))
      .WithStatus<TPMError>("Failed to call Ospi_Key_LoadKey");

  // Get the key blob so we can load it later.
  uint32_t length = 0;
  ScopedTssMemory buf(overalls, user_context.context);
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_GetAttribData(
                      pcr_bound_key, TSS_TSPATTRIB_KEY_BLOB,
                      TSS_TSPATTRIB_KEYBLOB_BLOB, &length, buf.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_GetAttribData");

  brillo::Blob key_blob(buf.value(), buf.value() + length);

  ASSIGN_OR_RETURN(
      const OperationPolicy& op_policy,
      backend_.config_.ToOperationPolicy(policy),
      _.WithStatus<TPMError>("Failed to convert setting to policy"));

  uint32_t key_handle = pcr_bound_key.value();
  KeyTpm1::Type key_type = KeyTpm1::Type::kTransientKey;
  std::optional<KeyReloadDataTpm1> reload_data;

  if (auto_reload) {
    key_type = KeyTpm1::Type::kReloadableTransientKey;
    reload_data = KeyReloadDataTpm1{
        .policy = op_policy,
        .key_blob = key_blob,
    };
  }

  ASSIGN_OR_RETURN(
      ScopedKey key,
      LoadKeyInternal(key_type, key_handle, std::move(pcr_bound_key),
                      /*reload_data=*/std::nullopt),
      _.WithStatus<TPMError>("Failed to load created RSA key"));

  return CreateKeyResult{
      .key = std::move(key),
      .key_blob = std::move(key_blob),
  };
}

StatusOr<KeyManagermentTpm1::CreateKeyResult>
KeyManagermentTpm1::CreateSoftwareGenRsaKey(
    const OperationPolicySetting& policy,
    const CreateKeyOptions& options,
    bool auto_reload) {
  brillo::SecureBlob public_modulus;
  brillo::SecureBlob prime_factor;
  if (!hwsec_foundation::CreateRsaKey(kDefaultTpmRsaKeyBits, &public_modulus,
                                      &prime_factor)) {
    return MakeStatus<TPMError>("Failed to creating software RSA key",
                                TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(ScopedKey srk,
                   GetPersistentKey(PersistentKeyType::kStorageRootKey));

  ASSIGN_OR_RETURN(const KeyTpm1& srk_data, GetKeyData(srk.GetKey()));

  ASSIGN_OR_RETURN(const TssTpmContext& user_context,
                   backend_.GetTssUserContext());

  // Create the key object
  TSS_FLAG init_flags =
      TSS_KEY_VOLATILE | TSS_KEY_MIGRATABLE | kDefaultTpmRsaKeyFlag;

  // In this case, the key is not decrypt only. It can be used to sign the
  // data too. No easy way to make a decrypt only key here.
  if (options.allow_sign && !options.allow_decrypt) {
    init_flags |= TSS_KEY_TYPE_SIGNING;
  } else {
    init_flags |= TSS_KEY_TYPE_LEGACY;
  }

  overalls::Overalls& overalls = backend_.overall_context_.overalls;

  ScopedTssKey local_key_handle(overalls, user_context.context);
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Context_CreateObject(
                      user_context.context, TSS_OBJECT_TYPE_RSAKEY, init_flags,
                      local_key_handle.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_Context_CreateObject");

  // Set the attributes
  if (options.allow_sign) {
    uint32_t sig_scheme = TSS_SS_RSASSAPKCS1V15_DER;
    RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_SetAttribUint32(
                        local_key_handle, TSS_TSPATTRIB_KEY_INFO,
                        TSS_TSPATTRIB_KEYINFO_SIGSCHEME, sig_scheme)))
        .WithStatus<TPMError>("Failed to call Ospi_SetAttribUint32");
  }

  if (options.allow_decrypt) {
    uint32_t enc_scheme = TSS_ES_RSAESPKCSV15;
    RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_SetAttribUint32(
                        local_key_handle, TSS_TSPATTRIB_KEY_INFO,
                        TSS_TSPATTRIB_KEYINFO_ENCSCHEME, enc_scheme)))
        .WithStatus<TPMError>("Failed to call Ospi_SetAttribUint32");
  }

  // Set a random migration policy password, and discard it.  The key will not
  // be migrated, but to create the key outside of the TPM, we have to do it
  // this way.
  ScopedTssPolicy policy_handle(overalls, user_context.context);
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Context_CreateObject(
                      user_context.context, TSS_OBJECT_TYPE_POLICY,
                      TSS_POLICY_MIGRATION, policy_handle.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_SetAttribUint32");

  brillo::SecureBlob migration_password =
      CreateSecureRandomBlob(kDefaultDiscardableWrapPasswordLength);
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Policy_SetSecret(
                      policy_handle, TSS_SECRET_MODE_PLAIN,
                      migration_password.size(), migration_password.data())))
      .WithStatus<TPMError>("Failed to call Ospi_Policy_SetSecret");

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Policy_AssignToObject(
                      policy_handle, local_key_handle)))
      .WithStatus<TPMError>("Failed to call Ospi_Policy_AssignToObject");

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_SetAttribData(
                      local_key_handle, TSS_TSPATTRIB_RSAKEY_INFO,
                      TSS_TSPATTRIB_KEYINFO_RSA_MODULUS, public_modulus.size(),
                      public_modulus.data())))
      .WithStatus<TPMError>("Failed to call Ospi_SetAttribData");

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_SetAttribData(
                      local_key_handle, TSS_TSPATTRIB_KEY_BLOB,
                      TSS_TSPATTRIB_KEYBLOB_PRIVATE_KEY, prime_factor.size(),
                      prime_factor.data())))
      .WithStatus<TPMError>("Failed to call Ospi_SetAttribData");

  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Key_WrapKey(
                      local_key_handle, srk_data.key_handle, 0)))
      .WithStatus<TPMError>("Failed to call Ospi_Key_WrapKey");

  uint32_t length = 0;
  ScopedTssMemory buf(overalls, user_context.context);
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_GetAttribData(
                      local_key_handle, TSS_TSPATTRIB_KEY_BLOB,
                      TSS_TSPATTRIB_KEYBLOB_BLOB, &length, buf.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_GetAttribData");

  brillo::Blob key_blob(buf.value(), buf.value() + length);

  ASSIGN_OR_RETURN(
      const OperationPolicy& op_policy,
      backend_.config_.ToOperationPolicy(policy),
      _.WithStatus<TPMError>("Failed to convert setting to policy"));

  ASSIGN_OR_RETURN(
      ScopedKey key,
      auto_reload ? LoadAutoReloadKey(op_policy, key_blob)
                  : LoadKey(op_policy, key_blob),
      _.WithStatus<TPMError>("Failed to load created software RSA key"));

  return CreateKeyResult{
      .key = std::move(key),
      .key_blob = std::move(key_blob),
  };
}

StatusOr<ScopedKey> KeyManagermentTpm1::LoadKey(const OperationPolicy& policy,
                                                const brillo::Blob& key_blob) {
  ASSIGN_OR_RETURN(ScopedTssKey key, LoadKeyBlob(policy, key_blob),
                   _.WithStatus<TPMError>("Failed to load key blob"));

  uint32_t key_handle = key.value();
  return LoadKeyInternal(KeyTpm1::Type::kTransientKey, key_handle,
                         std::move(key), /*reload_data=*/std::nullopt);
}

StatusOr<ScopedKey> KeyManagermentTpm1::LoadAutoReloadKey(
    const OperationPolicy& policy, const brillo::Blob& key_blob) {
  ASSIGN_OR_RETURN(ScopedTssKey key, LoadKeyBlob(policy, key_blob),
                   _.WithStatus<TPMError>("Failed to load key blob"));

  uint32_t key_handle = key.value();
  return LoadKeyInternal(KeyTpm1::Type::kReloadableTransientKey, key_handle,
                         std::move(key),
                         KeyReloadDataTpm1{
                             .policy = policy,
                             .key_blob = key_blob,
                         });
}

StatusOr<ScopedTssKey> KeyManagermentTpm1::LoadKeyBlob(
    const OperationPolicy& policy, const brillo::Blob& key_blob) {
  ASSIGN_OR_RETURN(ScopedKey srk,
                   GetPersistentKey(PersistentKeyType::kStorageRootKey));

  ASSIGN_OR_RETURN(const KeyTpm1& srk_data, GetKeyData(srk.GetKey()));

  ASSIGN_OR_RETURN(const TssTpmContext& user_context,
                   backend_.GetTssUserContext());

  overalls::Overalls& overalls = backend_.overall_context_.overalls;

  ScopedTssKey local_key_handle(overalls, user_context.context);
  brillo::Blob mutable_key_blob = key_blob;
  RETURN_IF_ERROR(
      MakeStatus<TPM1Error>(overalls.Ospi_Context_LoadKeyByBlob(
          user_context.context, srk_data.key_handle, mutable_key_blob.size(),
          mutable_key_blob.data(), local_key_handle.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_Context_LoadKeyByBlob");

  return local_key_handle;
}

StatusOr<ScopedKey> KeyManagermentTpm1::GetPersistentKey(
    PersistentKeyType key_type) {
  auto it = persistent_key_map_.find(key_type);
  if (it != persistent_key_map_.end()) {
    return ScopedKey(Key{.token = it->second}, backend_.middleware_derivative_);
  }

  uint32_t key_handle = 0;

  switch (key_type) {
    case PersistentKeyType::kStorageRootKey: {
      ASSIGN_OR_RETURN(key_handle, GetSrk(),
                       _.WithStatus<TPMError>("Failed to get SRK"));
    } break;
    default:
      return MakeStatus<TPMError>("Unknown persistent key type",
                                  TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(
      ScopedKey key,
      LoadKeyInternal(KeyTpm1::Type::kPersistentKey, key_handle,
                      /*scoped_key=*/std::nullopt,
                      /*reload_data=*/std::nullopt),
      _.WithStatus<TPMError>("Failed to side load persistent key"));

  persistent_key_map_[key_type] = key.GetKey().token;
  return key;
}

StatusOr<brillo::Blob> KeyManagermentTpm1::GetPubkeyHash(Key key) {
  ASSIGN_OR_RETURN(const KeyTpm1& key_data, GetKeyData(key));

  return Sha1(key_data.cache.pubkey_blob);
}

StatusOr<ScopedKey> KeyManagermentTpm1::SideLoadKey(uint32_t key_handle) {
  return LoadKeyInternal(KeyTpm1::Type::kPersistentKey, key_handle,
                         /*scoped_key=*/std::nullopt,
                         /*reload_data=*/std::nullopt);
}

StatusOr<uint32_t> KeyManagermentTpm1::GetKeyHandle(Key key) {
  ASSIGN_OR_RETURN(const KeyTpm1& key_data, GetKeyData(key));

  return key_data.key_handle;
}

StatusOr<brillo::Blob> KeyManagermentTpm1::GetPubkeyBlob(uint32_t key_handle) {
  ASSIGN_OR_RETURN(const TssTpmContext& user_context,
                   backend_.GetTssUserContext());

  overalls::Overalls& overalls = backend_.overall_context_.overalls;

  uint32_t size;
  ScopedTssMemory public_blob(overalls, user_context.context);
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Key_GetPubKey(
                      key_handle, &size, public_blob.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_Key_GetPubKey");

  return brillo::Blob(public_blob.value(), public_blob.value() + size);
}

StatusOr<ScopedKey> KeyManagermentTpm1::LoadKeyInternal(
    KeyTpm1::Type key_type,
    uint32_t key_handle,
    std::optional<ScopedTssKey> scoped_key,
    std::optional<KeyReloadDataTpm1> reload_data) {
  ASSIGN_OR_RETURN(brillo::Blob && pubkey_blob, GetPubkeyBlob(key_handle),
                   _.WithStatus<TPMError>("Failed to get pubkey blob"));

  KeyToken token = current_token_++;
  key_map_.emplace(token, KeyTpm1{
                              .type = key_type,
                              .key_handle = key_handle,
                              .cache =
                                  KeyTpm1::Cache{
                                      .pubkey_blob = std::move(pubkey_blob),
                                  },
                              .scoped_key = std::move(scoped_key),
                              .reload_data = std::move(reload_data),
                          });

  return ScopedKey(Key{.token = token}, backend_.middleware_derivative_);
}

Status KeyManagermentTpm1::Flush(Key key) {
  ASSIGN_OR_RETURN(const KeyTpm1& key_data, GetKeyData(key));

  switch (key_data.type) {
    case KeyTpm1::Type::kPersistentKey:
      // We don't need to unload these kinds of key.
      return OkStatus();

    case KeyTpm1::Type::kTransientKey:
    case KeyTpm1::Type::kReloadableTransientKey:
      key_map_.erase(key.token);
      return OkStatus();

    default:
      return MakeStatus<TPMError>("Unknown key type", TPMRetryAction::kNoRetry);
  }
}

StatusOr<std::reference_wrapper<KeyTpm1>> KeyManagermentTpm1::GetKeyData(
    Key key) {
  auto it = key_map_.find(key.token);
  if (it == key_map_.end()) {
    return MakeStatus<TPMError>("Unknown key", TPMRetryAction::kNoRetry);
  }
  return it->second;
}

Status KeyManagermentTpm1::ReloadIfPossible(Key key) {
  ASSIGN_OR_RETURN(KeyTpm1 & key_data, GetKeyData(key));

  if (key_data.type != KeyTpm1::Type::kReloadableTransientKey) {
    // We don't need to reload un-reloadable key.
    return OkStatus();
  }

  if (!key_data.reload_data.has_value()) {
    return MakeStatus<TPMError>("Empty reload data", TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(
      ScopedTssKey scoped_key,
      LoadKeyBlob(key_data.reload_data->policy, key_data.reload_data->key_blob),
      _.WithStatus<TPMError>("Failed to load key blob"));

  key_data.key_handle = scoped_key.value();
  key_data.scoped_key = std::move(scoped_key);

  return OkStatus();
}

StatusOr<uint32_t> KeyManagermentTpm1::GetSrk() {
  if (srk_cache_.has_value()) {
    return srk_cache_->value();
  }

  ASSIGN_OR_RETURN(bool is_ready, backend_.state_.IsReady(),
                   _.WithStatus<TPMError>("Failed to get the ready state"));

  if (!is_ready) {
    return MakeStatus<TPMError>("TPM is not ready", TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(const TssTpmContext& user_context,
                   backend_.GetTssUserContext());

  overalls::Overalls& overalls = backend_.overall_context_.overalls;

  // Load the Storage Root Key
  TSS_UUID SRK_UUID = TSS_UUID_SRK;
  ScopedTssKey local_srk_handle(overalls, user_context.context);
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Context_LoadKeyByUUID(
                      user_context.context, TSS_PS_TYPE_SYSTEM, SRK_UUID,
                      local_srk_handle.ptr())))
      .WithStatus<TPMError>("Failed to call Ospi_Context_LoadKeyByUUID");

  // Check if the SRK wants a password
  uint32_t srk_authusage;
  RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_GetAttribUint32(
                      local_srk_handle, TSS_TSPATTRIB_KEY_INFO,
                      TSS_TSPATTRIB_KEYINFO_AUTHUSAGE, &srk_authusage)))
      .WithStatus<TPMError>("Failed to call Ospi_GetAttribUint32");

  // Give it the password if needed
  if (srk_authusage) {
    TSS_HPOLICY srk_usage_policy;
    RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_GetPolicyObject(
                        local_srk_handle, TSS_POLICY_USAGE, &srk_usage_policy)))
        .WithStatus<TPMError>("Failed to call Ospi_GetPolicyObject");

    brillo::Blob srk_auth(kDefaultSrkAuth,
                          kDefaultSrkAuth + sizeof(kDefaultSrkAuth));
    RETURN_IF_ERROR(MakeStatus<TPM1Error>(overalls.Ospi_Policy_SetSecret(
                        srk_usage_policy, TSS_SECRET_MODE_PLAIN,
                        srk_auth.size(), srk_auth.data())))
        .WithStatus<TPMError>("Failed to call Ospi_Policy_SetSecret");
  }

  srk_cache_ = std::move(local_srk_handle);
  return srk_cache_->value();
}

}  // namespace hwsec
