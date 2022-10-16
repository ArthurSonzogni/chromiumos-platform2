// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/backend/tpm2/key_management.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <absl/container/flat_hash_set.h>
#include <base/callback_helpers.h>
#include <base/numerics/safe_conversions.h>
#include <brillo/secure_blob.h>
#include <crypto/scoped_openssl_types.h>
#include <libhwsec-foundation/crypto/rsa.h>
#include <libhwsec-foundation/crypto/sha.h>
#include <libhwsec-foundation/status/status_chain_macros.h>
#include <trunks/tpm_utility.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "libhwsec/backend/tpm2/backend.h"
#include "libhwsec/error/tpm2_error.h"
#include "libhwsec/status.h"

using brillo::BlobFromString;
using brillo::BlobToString;
using hwsec_foundation::Sha256;
using hwsec_foundation::status::MakeStatus;

namespace hwsec {

namespace {

constexpr uint32_t kDefaultTpmRsaKeyBits = 2048;
constexpr uint32_t kDefaultTpmRsaModulusSize = 2048;
constexpr uint32_t kDefaultTpmPublicExponent = 0x10001;
constexpr trunks::TPMI_ECC_CURVE kDefaultTpmCurveId = trunks::TPM_ECC_NIST_P256;

constexpr struct {
  trunks::TPM_ALG_ID trunks_id;
  int openssl_nid;
} kSupportedECCurveAlgorithms[] = {
    {trunks::TPM_ECC_NIST_P256, NID_X9_62_prime256v1},
};

StatusOr<trunks::TpmUtility::AsymmetricKeyUsage> GetKeyUsage(
    const KeyManagementTpm2::CreateKeyOptions& options) {
  if (options.allow_decrypt == true && options.allow_sign == true) {
    return trunks::TpmUtility::AsymmetricKeyUsage::kDecryptAndSignKey;
  } else if (options.allow_decrypt == true && options.allow_sign == false) {
    return trunks::TpmUtility::AsymmetricKeyUsage::kDecryptKey;
  } else if (options.allow_decrypt == false && options.allow_sign == true) {
    return trunks::TpmUtility::AsymmetricKeyUsage::kSignKey;
  } else {
    return MakeStatus<TPMError>("Useless key", TPMRetryAction::kNoRetry);
  }
}

struct RsaParameters {
  uint32_t key_exponent;
  brillo::Blob key_modulus;
};

StatusOr<RsaParameters> ParseSpkiDer(const brillo::Blob& public_key_spki_der) {
  // Parse the SPKI.
  const unsigned char* asn1_ptr = public_key_spki_der.data();
  const crypto::ScopedEVP_PKEY pkey(
      d2i_PUBKEY(nullptr, &asn1_ptr, public_key_spki_der.size()));
  if (!pkey) {
    return MakeStatus<TPMError>("Failed to parse Subject Public Key Info DER",
                                TPMRetryAction::kNoRetry);
  }

  const crypto::ScopedRSA rsa(EVP_PKEY_get1_RSA(pkey.get()));
  if (!rsa) {
    return MakeStatus<TPMError>("non-RSA key was supplied",
                                TPMRetryAction::kNoRetry);
  }

  brillo::Blob key_modulus(RSA_size(rsa.get()));
  const BIGNUM* n;
  const BIGNUM* e;
  RSA_get0_key(rsa.get(), &n, &e, nullptr);
  if (BN_bn2bin(n, key_modulus.data()) != key_modulus.size()) {
    return MakeStatus<TPMError>("Failed to extract public key modulus",
                                TPMRetryAction::kNoRetry);
  }

  constexpr BN_ULONG kInvalidBnWord = ~static_cast<BN_ULONG>(0);
  const BN_ULONG exponent_word = BN_get_word(e);
  if (exponent_word == kInvalidBnWord ||
      !base::IsValueInRangeForNumericType<uint32_t>(exponent_word)) {
    return MakeStatus<TPMError>("Failed to extract public key exponent",
                                TPMRetryAction::kNoRetry);
  }

  const uint32_t key_exponent = static_cast<uint32_t>(exponent_word);

  return RsaParameters{
      .key_exponent = key_exponent,
      .key_modulus = std::move(key_modulus),
  };
}

StatusOr<uint32_t> GetIntegerExponent(const brillo::Blob& public_exponent) {
  if (public_exponent.size() > 4) {
    return MakeStatus<TPMError>("Exponent too large", TPMRetryAction::kNoRetry);
  }

  uint32_t exponent = 0;
  for (size_t i = 0; i < public_exponent.size(); i++) {
    exponent = exponent << 8;
    exponent += public_exponent[i];
  }
  return exponent;
}

StatusOr<trunks::TPMI_ECC_CURVE> ConvertNIDToTrunksCurveID(int curve_nid) {
  for (const auto& curve_info : kSupportedECCurveAlgorithms) {
    if (curve_info.openssl_nid == curve_nid) {
      return curve_info.trunks_id;
    }
  }
  return MakeStatus<TPMError>("Unsupported curve", TPMRetryAction::kNoRetry);
}

// Padding '\0' at the beginning of the string until it matches the length.
// This is for padding elliptic curve points and keys, and not for ordinary
// string. It's needed to normalize the format of the curve point.
std::string PaddingStringToLength(const std::string& in, size_t length) {
  if (in.length() < length) {
    return std::string(length - in.length(), '\0') + in;
  }
  return in;
}

}  // namespace

KeyManagementTpm2::~KeyManagementTpm2() {
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
KeyManagementTpm2::GetSupportedAlgo() {
  return absl::flat_hash_set<KeyAlgoType>({
      KeyAlgoType::kRsa,
      KeyAlgoType::kEcc,
  });
}

StatusOr<KeyManagementTpm2::CreateKeyResult> KeyManagementTpm2::CreateKey(
    const OperationPolicySetting& policy,
    KeyAlgoType key_algo,
    AutoReload auto_reload,
    const CreateKeyOptions& options) {
  switch (key_algo) {
    case KeyAlgoType::kRsa:
      return CreateRsaKey(policy, options, auto_reload);
    case KeyAlgoType::kEcc:
      return CreateEccKey(policy, options, auto_reload);
    default:
      return MakeStatus<TPMError>("Unsupported key creation algorithm",
                                  TPMRetryAction::kNoRetry);
  }
}

StatusOr<KeyManagementTpm2::CreateKeyResult> KeyManagementTpm2::CreateRsaKey(
    const OperationPolicySetting& policy,
    const CreateKeyOptions& options,
    AutoReload auto_reload) {
  ASSIGN_OR_RETURN(
      const ConfigTpm2::PcrMap& setting,
      backend_.GetConfigTpm2().ToSettingsPcrMap(policy.device_config_settings),
      _.WithStatus<TPMError>("Failed to convert setting to PCR map"));

  if (options.allow_software_gen && setting.empty()) {
    return CreateSoftwareGenRsaKey(policy, options, auto_reload);
  }

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  ASSIGN_OR_RETURN(trunks::TpmUtility::AsymmetricKeyUsage usage,
                   GetKeyUsage(options),
                   _.WithStatus<TPMError>("Failed to get key usage"));

  std::string policy_digest;
  std::vector<uint32_t> pcr_list;
  bool use_only_policy_authorization = false;

  if (!setting.empty()) {
    RETURN_IF_ERROR(
        MakeStatus<TPM2Error>(context.tpm_utility->GetPolicyDigestForPcrValues(
            setting, policy.permission.auth_value.has_value(), &policy_digest)))
        .WithStatus<TPMError>("Failed to get policy digest");

    for (const auto& map_pair : setting) {
      pcr_list.push_back(map_pair.first);
    }

    // We should not allow using the key without policy when the policy had been
    // set.
    use_only_policy_authorization = true;
  }

  std::string auth_value;
  if (policy.permission.auth_value.has_value()) {
    auth_value = policy.permission.auth_value.value().to_string();
  }

  // Cleanup the data from secure blob.
  base::ScopedClosureRunner cleanup_auth_value(base::BindOnce(
      brillo::SecureClearContainer<std::string>, std::ref(auth_value)));

  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context.factory.GetPasswordAuthorization("");

  std::string tpm_key_blob;

  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context.tpm_utility->CreateRSAKeyPair(
          usage, kDefaultTpmRsaModulusSize, kDefaultTpmPublicExponent,
          auth_value, policy_digest, use_only_policy_authorization, pcr_list,
          delegate.get(), &tpm_key_blob, nullptr /* No creation_blob */)))
      .WithStatus<TPMError>("Failed to create RSA key");

  brillo::Blob key_blob = BlobFromString(tpm_key_blob);

  ASSIGN_OR_RETURN(
      const OperationPolicy& op_policy,
      backend_.GetConfigTpm2().ToOperationPolicy(policy),
      _.WithStatus<TPMError>("Failed to convert setting to policy"));

  ASSIGN_OR_RETURN(ScopedKey key, LoadKey(op_policy, key_blob, auto_reload),
                   _.WithStatus<TPMError>("Failed to load created RSA key"));

  return CreateKeyResult{
      .key = std::move(key),
      .key_blob = std::move(key_blob),
  };
}

StatusOr<KeyManagementTpm2::CreateKeyResult>
KeyManagementTpm2::CreateSoftwareGenRsaKey(const OperationPolicySetting& policy,
                                           const CreateKeyOptions& options,
                                           AutoReload auto_reload) {
  brillo::SecureBlob n;
  brillo::SecureBlob p;
  if (!hwsec_foundation::CreateRsaKey(kDefaultTpmRsaKeyBits, &n, &p)) {
    return MakeStatus<TPMError>("Failed to creating software RSA key",
                                TPMRetryAction::kNoRetry);
  }

  return WrapRSAKey(policy, brillo::Blob(std::begin(n), std::end(n)), p,
                    auto_reload, options);
}

StatusOr<KeyManagementTpm2::CreateKeyResult> KeyManagementTpm2::WrapRSAKey(
    const OperationPolicySetting& policy,
    const brillo::Blob& public_modulus,
    const brillo::SecureBlob& private_prime_factor,
    AutoReload auto_reload,
    const CreateKeyOptions& options) {
  ASSIGN_OR_RETURN(
      const ConfigTpm2::PcrMap& setting,
      backend_.GetConfigTpm2().ToSettingsPcrMap(policy.device_config_settings),
      _.WithStatus<TPMError>("Failed to convert setting to PCR map"));

  if (!setting.empty()) {
    return MakeStatus<TPMError>("Unsupported device config",
                                TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(trunks::TpmUtility::AsymmetricKeyUsage usage,
                   GetKeyUsage(options),
                   _.WithStatus<TPMError>("Failed to get key usage"));

  std::string prime_factor = private_prime_factor.to_string();

  std::string auth_value;
  if (policy.permission.auth_value.has_value()) {
    auth_value = policy.permission.auth_value.value().to_string();
  }

  // Cleanup the data from secure blob.
  base::ScopedClosureRunner cleanup_prime_factor(base::BindOnce(
      brillo::SecureClearContainer<std::string>, std::ref(prime_factor)));
  base::ScopedClosureRunner cleanup_auth_value(base::BindOnce(
      brillo::SecureClearContainer<std::string>, std::ref(auth_value)));

  uint32_t exponent = kDefaultTpmPublicExponent;
  if (options.rsa_exponent.has_value()) {
    ASSIGN_OR_RETURN(exponent,
                     GetIntegerExponent(options.rsa_exponent.value()));
  }

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  std::string tpm_key_blob;
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context.factory.GetPasswordAuthorization("");

  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context.tpm_utility->ImportRSAKey(
                      usage, brillo::BlobToString(public_modulus), exponent,
                      prime_factor, auth_value, delegate.get(), &tpm_key_blob)))
      .WithStatus<TPMError>("Failed to import software RSA key");

  brillo::Blob key_blob = BlobFromString(tpm_key_blob);

  ASSIGN_OR_RETURN(
      const OperationPolicy& op_policy,
      backend_.GetConfigTpm2().ToOperationPolicy(policy),
      _.WithStatus<TPMError>("Failed to convert setting to policy"));

  ASSIGN_OR_RETURN(
      ScopedKey key, LoadKey(op_policy, key_blob, auto_reload),
      _.WithStatus<TPMError>("Failed to load created software RSA key"));

  return CreateKeyResult{
      .key = std::move(key),
      .key_blob = std::move(key_blob),
  };
}

StatusOr<KeyManagementTpm2::CreateKeyResult> KeyManagementTpm2::WrapECCKey(
    const OperationPolicySetting& policy,
    const brillo::Blob& public_point_x,
    const brillo::Blob& public_point_y,
    const brillo::SecureBlob& private_value,
    AutoReload auto_reload,
    const CreateKeyOptions& options) {
  ASSIGN_OR_RETURN(
      const ConfigTpm2::PcrMap& setting,
      backend_.GetConfigTpm2().ToSettingsPcrMap(policy.device_config_settings),
      _.WithStatus<TPMError>("Failed to convert setting to PCR map"));

  if (!setting.empty()) {
    return MakeStatus<TPMError>("Unsupported device config",
                                TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(trunks::TpmUtility::AsymmetricKeyUsage usage,
                   GetKeyUsage(options),
                   _.WithStatus<TPMError>("Failed to get key usage"));

  std::string auth_value;
  if (policy.permission.auth_value.has_value()) {
    auth_value = policy.permission.auth_value.value().to_string();
  }

  // Cleanup the data from secure blob.
  base::ScopedClosureRunner cleanup_auth_value(base::BindOnce(
      brillo::SecureClearContainer<std::string>, std::ref(auth_value)));

  trunks::TPMI_ECC_CURVE curve = kDefaultTpmCurveId;

  if (options.ecc_nid.has_value()) {
    ASSIGN_OR_RETURN(curve, ConvertNIDToTrunksCurveID(options.ecc_nid.value()));
  }

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  std::string tpm_key_blob;
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context.factory.GetPasswordAuthorization("");

  RETURN_IF_ERROR(
      MakeStatus<TPM2Error>(context.tpm_utility->ImportECCKey(
          usage, curve,
          PaddingStringToLength(brillo::BlobToString(public_point_x),
                                MAX_ECC_KEY_BYTES),
          PaddingStringToLength(brillo::BlobToString(public_point_y),
                                MAX_ECC_KEY_BYTES),
          PaddingStringToLength(private_value.to_string(), MAX_ECC_KEY_BYTES),
          auth_value, delegate.get(), &tpm_key_blob)))
      .WithStatus<TPMError>("Failed to import software RSA key");

  brillo::Blob key_blob = BlobFromString(tpm_key_blob);

  ASSIGN_OR_RETURN(
      const OperationPolicy& op_policy,
      backend_.GetConfigTpm2().ToOperationPolicy(policy),
      _.WithStatus<TPMError>("Failed to convert setting to policy"));

  ASSIGN_OR_RETURN(
      ScopedKey key, LoadKey(op_policy, key_blob, auto_reload),
      _.WithStatus<TPMError>("Failed to load created software RSA key"));

  return CreateKeyResult{
      .key = std::move(key),
      .key_blob = std::move(key_blob),
  };
}

StatusOr<KeyManagementTpm2::CreateKeyResult> KeyManagementTpm2::CreateEccKey(
    const OperationPolicySetting& policy,
    const CreateKeyOptions& options,
    AutoReload auto_reload) {
  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  ASSIGN_OR_RETURN(trunks::TpmUtility::AsymmetricKeyUsage usage,
                   GetKeyUsage(options),
                   _.WithStatus<TPMError>("Failed to get key usage"));

  std::string policy_digest;
  std::vector<uint32_t> pcr_list;
  bool use_only_policy_authorization = false;

  ASSIGN_OR_RETURN(
      const ConfigTpm2::PcrMap& setting,
      backend_.GetConfigTpm2().ToSettingsPcrMap(policy.device_config_settings),
      _.WithStatus<TPMError>("Failed to convert setting to PCR map"));

  if (!setting.empty()) {
    RETURN_IF_ERROR(
        MakeStatus<TPM2Error>(context.tpm_utility->GetPolicyDigestForPcrValues(
            setting, policy.permission.auth_value.has_value(), &policy_digest)))
        .WithStatus<TPMError>("Failed to get policy digest");

    for (const auto& map_pair : setting) {
      pcr_list.push_back(map_pair.first);
    }

    // We should not allow using the key without policy when the policy had been
    // set.
    use_only_policy_authorization = true;
  }

  std::string auth_value;
  if (policy.permission.auth_value.has_value()) {
    auth_value = policy.permission.auth_value.value().to_string();
  }

  // Cleanup the data from secure blob.
  base::ScopedClosureRunner cleanup_auth_value(base::BindOnce(
      brillo::SecureClearContainer<std::string>, std::ref(auth_value)));

  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context.factory.GetPasswordAuthorization("");

  std::string tpm_key_blob;

  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context.tpm_utility->CreateECCKeyPair(
                      usage, kDefaultTpmCurveId, auth_value, policy_digest,
                      use_only_policy_authorization, pcr_list, delegate.get(),
                      &tpm_key_blob, /*creation_blob=*/nullptr)))
      .WithStatus<TPMError>("Failed to create ECC key");

  brillo::Blob key_blob = BlobFromString(tpm_key_blob);

  ASSIGN_OR_RETURN(
      const OperationPolicy& op_policy,
      backend_.GetConfigTpm2().ToOperationPolicy(policy),
      _.WithStatus<TPMError>("Failed to convert setting to policy"));

  ASSIGN_OR_RETURN(ScopedKey key, LoadKey(op_policy, key_blob, auto_reload),
                   _.WithStatus<TPMError>("Failed to load created RSA key"));

  return CreateKeyResult{
      .key = std::move(key),
      .key_blob = std::move(key_blob),
  };
}

StatusOr<ScopedKey> KeyManagementTpm2::LoadKey(const OperationPolicy& policy,
                                               const brillo::Blob& key_blob,
                                               AutoReload auto_reload) {
  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  uint32_t key_handle;
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context.factory.GetPasswordAuthorization("");

  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context.tpm_utility->LoadKey(
                      BlobToString(key_blob), delegate.get(), &key_handle)))
      .WithStatus<TPMError>("Failed to load SRK wrapped key");

  KeyTpm2::Type key_type = KeyTpm2::Type::kTransientKey;
  std::optional<KeyReloadDataTpm2> reload_data;
  if (auto_reload == AutoReload::kTrue) {
    key_type = KeyTpm2::Type::kReloadableTransientKey;
    reload_data = KeyReloadDataTpm2{
        .key_blob = key_blob,
    };
  }

  return LoadKeyInternal(policy, key_type, key_handle, reload_data);
}

StatusOr<ScopedKey> KeyManagementTpm2::GetPersistentKey(
    PersistentKeyType key_type) {
  auto it = persistent_key_map_.find(key_type);
  if (it != persistent_key_map_.end()) {
    return ScopedKey(Key{.token = it->second},
                     backend_.GetMiddlewareDerivative());
  }

  uint32_t key_handle = 0;

  switch (key_type) {
    case PersistentKeyType::kStorageRootKey:
      key_handle = trunks::kStorageRootKey;
      break;
    default:
      return MakeStatus<TPMError>("Unknown persistent key type",
                                  TPMRetryAction::kNoRetry);
  }

  ASSIGN_OR_RETURN(
      ScopedKey key,
      LoadKeyInternal(OperationPolicy{}, KeyTpm2::Type::kPersistentKey,
                      key_handle,
                      /*reload_data=*/std::nullopt),
      _.WithStatus<TPMError>("Failed to side load persistent key"));

  persistent_key_map_[key_type] = key.GetKey().token;

  return key;
}

StatusOr<brillo::Blob> KeyManagementTpm2::GetPubkeyHash(Key key) {
  ASSIGN_OR_RETURN(const KeyTpm2& key_data, GetKeyData(key));

  const trunks::TPMT_PUBLIC& public_data = key_data.cache.public_area;
  if (public_data.type == trunks::TPM_ALG_RSA) {
    std::string public_modulus =
        trunks::StringFrom_TPM2B_PUBLIC_KEY_RSA(public_data.unique.rsa);
    return Sha256(BlobFromString(public_modulus));
  } else if (public_data.type == trunks::TPM_ALG_ECC) {
    std::string x_point =
        trunks::StringFrom_TPM2B_ECC_PARAMETER(public_data.unique.ecc.x);
    return Sha256(BlobFromString(x_point));
  }

  return MakeStatus<TPMError>("Unknown key algorithm",
                              TPMRetryAction::kNoRetry);
}

StatusOr<ScopedKey> KeyManagementTpm2::SideLoadKey(uint32_t key_handle) {
  return LoadKeyInternal(OperationPolicy{}, KeyTpm2::Type::kPersistentKey,
                         key_handle,
                         /*reload_data=*/std::nullopt);
}

StatusOr<uint32_t> KeyManagementTpm2::GetKeyHandle(Key key) {
  ASSIGN_OR_RETURN(const KeyTpm2& key_data, GetKeyData(key));

  return key_data.key_handle;
}

StatusOr<ScopedKey> KeyManagementTpm2::LoadKeyInternal(
    const OperationPolicy& policy,
    KeyTpm2::Type key_type,
    uint32_t key_handle,
    std::optional<KeyReloadDataTpm2> reload_data) {
  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  trunks::TPMT_PUBLIC public_area;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context.tpm_utility->GetKeyPublicArea(
                      key_handle, &public_area)))
      .WithStatus<TPMError>("Failed to Get key public area");

  KeyToken token = current_token_++;
  key_map_.emplace(token, KeyTpm2{
                              .type = key_type,
                              .key_handle = key_handle,
                              .cache =
                                  KeyTpm2::Cache{
                                      .policy = policy,
                                      .public_area = std::move(public_area),
                                  },
                              .reload_data = std::move(reload_data),
                          });

  return ScopedKey(Key{.token = token}, backend_.GetMiddlewareDerivative());
}

Status KeyManagementTpm2::Flush(Key key) {
  ASSIGN_OR_RETURN(const KeyTpm2& key_data, GetKeyData(key));

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();
  switch (key_data.type) {
    case KeyTpm2::Type::kPersistentKey:
      // We don't need to unload these kinds of key.
      return OkStatus();

    case KeyTpm2::Type::kTransientKey:
    case KeyTpm2::Type::kReloadableTransientKey:
      RETURN_IF_ERROR(
          MakeStatus<TPM2Error>(context.factory.GetTpm()->FlushContextSync(
              key_data.key_handle, nullptr)))
          .WithStatus<TPMError>("Failed to flush key handle");
      key_map_.erase(key.token);
      return OkStatus();

    default:
      return MakeStatus<TPMError>("Unknown key type", TPMRetryAction::kNoRetry);
  }
}

StatusOr<std::reference_wrapper<KeyTpm2>> KeyManagementTpm2::GetKeyData(
    Key key) {
  auto it = key_map_.find(key.token);
  if (it == key_map_.end()) {
    return MakeStatus<TPMError>("Unknown key", TPMRetryAction::kNoRetry);
  }
  return it->second;
}

Status KeyManagementTpm2::ReloadIfPossible(Key key) {
  ASSIGN_OR_RETURN(KeyTpm2 & key_data, GetKeyData(key));

  if (key_data.type != KeyTpm2::Type::kReloadableTransientKey) {
    // We don't need to reload un-reloadable key.
    return OkStatus();
  }

  if (!key_data.reload_data.has_value()) {
    return MakeStatus<TPMError>("Empty reload data", TPMRetryAction::kNoRetry);
  }

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();

  if (auto status =
          MakeStatus<TPM2Error>(context.factory.GetTpm()->FlushContextSync(
              key_data.key_handle, nullptr));
      !status.ok()) {
    LOG(WARNING) << "Failed to flush stale key handle: " << status;
  }

  uint32_t key_handle;
  std::unique_ptr<trunks::AuthorizationDelegate> delegate =
      context.factory.GetPasswordAuthorization("");
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context.tpm_utility->LoadKey(
                      BlobToString(key_data.reload_data->key_blob),
                      delegate.get(), &key_handle)))
      .WithStatus<TPMError>("Failed to reload SRK wrapped key");

  key_data.key_handle = key_handle;
  return OkStatus();
}

StatusOr<ScopedKey> KeyManagementTpm2::LoadPublicKeyFromSpki(
    const brillo::Blob& public_key_spki_der,
    trunks::TPM_ALG_ID scheme,
    trunks::TPM_ALG_ID hash_alg) {
  ASSIGN_OR_RETURN(const RsaParameters& public_key,
                   ParseSpkiDer(public_key_spki_der));

  BackendTpm2::TrunksClientContext& context = backend_.GetTrunksContext();
  // Load the key into the TPM.
  trunks::TPM_HANDLE key_handle = 0;
  RETURN_IF_ERROR(MakeStatus<TPM2Error>(context.tpm_utility->LoadRSAPublicKey(
                      trunks::TpmUtility::AsymmetricKeyUsage::kSignKey, scheme,
                      hash_alg, brillo::BlobToString(public_key.key_modulus),
                      public_key.key_exponent, nullptr, &key_handle)))
      .WithStatus<TPMError>("Failed to load RSA public key");

  return LoadKeyInternal(OperationPolicy{}, KeyTpm2::Type::kTransientKey,
                         key_handle,
                         /*reload_data=*/std::nullopt);
}

}  // namespace hwsec
