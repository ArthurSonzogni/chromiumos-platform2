// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_KEY_MANAGEMENT_H_
#define LIBHWSEC_BACKEND_TPM2_KEY_MANAGEMENT_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <brillo/secure_blob.h>
#include <trunks/tpm_generated.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/no_default_init.h"
#include "libhwsec/structures/operation_policy.h"

namespace hwsec {

class BackendTpm2;

struct KeyReloadDataTpm2 {
  brillo::Blob key_blob;
};

struct KeyTpm2 {
  enum class Type {
    kPersistentKey,
    kTransientKey,
    kReloadableTransientKey,
  };

  struct Cache {
    OperationPolicy policy;
    NoDefault<trunks::TPMT_PUBLIC> public_area;
  };

  NoDefault<Type> type;
  NoDefault<trunks::TPM_HANDLE> key_handle;
  NoDefault<Cache> cache;
  std::optional<KeyReloadDataTpm2> reload_data;
};

class KeyManagementTpm2 : public Backend::KeyManagement,
                          public Backend::SubClassHelper<BackendTpm2> {
 public:
  using SubClassHelper::SubClassHelper;
  ~KeyManagementTpm2();

  StatusOr<absl::flat_hash_set<KeyAlgoType>> GetSupportedAlgo() override;
  StatusOr<CreateKeyResult> CreateKey(const OperationPolicySetting& policy,
                                      KeyAlgoType key_algo,
                                      AutoReload auto_reload,
                                      const CreateKeyOptions& options) override;
  StatusOr<ScopedKey> LoadKey(const OperationPolicy& policy,
                              const brillo::Blob& key_blob,
                              AutoReload auto_reload) override;
  StatusOr<ScopedKey> GetPersistentKey(PersistentKeyType key_type) override;
  StatusOr<brillo::Blob> GetPubkeyHash(Key key) override;
  Status Flush(Key key) override;
  Status ReloadIfPossible(Key key) override;

  StatusOr<ScopedKey> SideLoadKey(uint32_t key_handle) override;
  StatusOr<uint32_t> GetKeyHandle(Key key) override;

  StatusOr<CreateKeyResult> WrapRSAKey(
      const OperationPolicySetting& policy,
      const brillo::Blob& public_modulus,
      const brillo::SecureBlob& private_prime_factor,
      AutoReload auto_reload,
      const CreateKeyOptions& options) override;
  StatusOr<CreateKeyResult> WrapECCKey(
      const OperationPolicySetting& policy,
      const brillo::Blob& public_point_x,
      const brillo::Blob& public_point_y,
      const brillo::SecureBlob& private_value,
      AutoReload auto_reload,
      const CreateKeyOptions& options) override;
  StatusOr<RSAPublicInfo> GetRSAPublicInfo(Key key) override;
  StatusOr<ECCPublicInfo> GetECCPublicInfo(Key key) override;

  // Below are TPM2.0 specific code.

  // Gets the reference of the internal key data.
  StatusOr<std::reference_wrapper<KeyTpm2>> GetKeyData(Key key);

  // Loads the key from its DER-encoded Subject Public Key Info. Algorithm
  // scheme and hashing algorithm are passed via |scheme| and |hash_alg|.
  // Currently, only the RSA signing keys are supported.
  StatusOr<ScopedKey> LoadPublicKeyFromSpki(
      const brillo::Blob& public_key_spki_der,
      trunks::TPM_ALG_ID scheme,
      trunks::TPM_ALG_ID hash_alg);

 private:
  StatusOr<CreateKeyResult> CreateRsaKey(const OperationPolicySetting& policy,
                                         const CreateKeyOptions& options,
                                         AutoReload auto_reload);
  StatusOr<CreateKeyResult> CreateSoftwareGenRsaKey(
      const OperationPolicySetting& policy,
      const CreateKeyOptions& options,
      AutoReload auto_reload);
  StatusOr<CreateKeyResult> CreateEccKey(const OperationPolicySetting& policy,
                                         const CreateKeyOptions& options,
                                         AutoReload auto_reload);
  StatusOr<ScopedKey> LoadKeyInternal(
      const OperationPolicy& policy,
      KeyTpm2::Type key_type,
      uint32_t key_handle,
      std::optional<KeyReloadDataTpm2> reload_data);

  KeyToken current_token_ = 0;
  absl::flat_hash_map<KeyToken, KeyTpm2> key_map_;
  absl::flat_hash_map<PersistentKeyType, KeyToken> persistent_key_map_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_KEY_MANAGEMENT_H_
