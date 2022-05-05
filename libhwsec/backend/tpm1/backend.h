// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM1_BACKEND_H_
#define LIBHWSEC_BACKEND_TPM1_BACKEND_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <base/gtest_prod_util.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <trousers/trousers.h>
#include <trousers/tss.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/backend/tpm1/key_managerment.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/proxy/proxy.h"
#include "libhwsec/tss_utils/scoped_tss_type.h"

namespace hwsec {

class BackendTpm1 : public Backend {
 public:
  class StateTpm1 : public State, public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<bool> IsEnabled() override;
    StatusOr<bool> IsReady() override;
    Status Prepare() override;
  };

  class SealingTpm1 : public Sealing, public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<brillo::Blob> Seal(
        const OperationPolicySetting& policy,
        const brillo::SecureBlob& unsealed_data) override;
    StatusOr<std::optional<ScopedKey>> PreloadSealedData(
        const OperationPolicy& policy,
        const brillo::Blob& sealed_data) override;
    StatusOr<brillo::SecureBlob> Unseal(const OperationPolicy& policy,
                                        const brillo::Blob& sealed_data,
                                        UnsealOptions options) override;

   private:
    StatusOr<ScopedTssKey> GetAuthValueKey(
        const brillo::SecureBlob& auth_value);
  };

  class DerivingTpm1 : public Deriving, public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<brillo::Blob> Derive(Key key, const brillo::Blob& blob) override;
    StatusOr<brillo::SecureBlob> SecureDerive(
        Key key, const brillo::SecureBlob& blob) override;
  };

  class KeyManagermentTpm1 : public KeyManagerment,
                             public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;

    ~KeyManagermentTpm1();
    StatusOr<absl::flat_hash_set<KeyAlgoType>> GetSupportedAlgo() override;
    StatusOr<CreateKeyResult> CreateKey(const OperationPolicySetting& policy,
                                        KeyAlgoType key_algo,
                                        CreateKeyOptions options) override;
    StatusOr<ScopedKey> LoadKey(const OperationPolicy& policy,
                                const brillo::Blob& key_blob) override;
    StatusOr<CreateKeyResult> CreateAutoReloadKey(
        const OperationPolicySetting& policy,
        KeyAlgoType key_algo,
        CreateKeyOptions options) override;
    StatusOr<ScopedKey> LoadAutoReloadKey(
        const OperationPolicy& policy, const brillo::Blob& key_blob) override;
    StatusOr<ScopedKey> GetPersistentKey(PersistentKeyType key_type) override;
    StatusOr<brillo::Blob> GetPubkeyHash(Key key) override;
    Status Flush(Key key) override;
    Status ReloadIfPossible(Key key) override;

    StatusOr<ScopedKey> SideLoadKey(uint32_t key_handle) override;
    StatusOr<uint32_t> GetKeyHandle(Key key) override;

    StatusOr<std::reference_wrapper<KeyTpm1>> GetKeyData(Key key);

   private:
    StatusOr<CreateKeyResult> CreateRsaKey(const OperationPolicySetting& policy,
                                           const CreateKeyOptions& options,
                                           bool auto_reload);
    StatusOr<CreateKeyResult> CreateSoftwareGenRsaKey(
        const OperationPolicySetting& policy,
        const CreateKeyOptions& options,
        bool auto_reload);
    StatusOr<ScopedTssKey> LoadKeyBlob(const OperationPolicy& policy,
                                       const brillo::Blob& key_blob);
    StatusOr<ScopedKey> LoadKeyInternal(
        KeyTpm1::Type key_type,
        uint32_t key_handle,
        std::optional<ScopedTssKey> scoped_key,
        std::optional<KeyReloadDataTpm1> reload_data);
    StatusOr<brillo::Blob> GetPubkeyBlob(uint32_t key_handle);
    StatusOr<uint32_t> GetSrk();

    KeyToken current_token_ = 0;
    absl::flat_hash_map<KeyToken, KeyTpm1> key_map_;
    absl::flat_hash_map<PersistentKeyType, KeyToken> persistent_key_map_;
    std::optional<ScopedTssKey> srk_cache_;
  };

  class ConfigTpm1 : public Config, public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<OperationPolicy> ToOperationPolicy(
        const OperationPolicySetting& policy) override;
    Status SetCurrentUser(const std::string& current_user) override;
    StatusOr<QuoteResult> Quote(DeviceConfigs device_config, Key key) override;

    using PcrMap = std::map<uint32_t, brillo::Blob>;
    StatusOr<PcrMap> ToPcrMap(const DeviceConfigs& device_config);
    StatusOr<PcrMap> ToSettingsPcrMap(const DeviceConfigSettings& settings);

   private:
    StatusOr<brillo::Blob> ReadPcr(uint32_t pcr_index);
  };

  class RandomTpm1 : public Random, public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<brillo::Blob> RandomBlob(size_t size) override;
    StatusOr<brillo::SecureBlob> RandomSecureBlob(size_t size) override;
  };

  BackendTpm1(Proxy& proxy, MiddlewareDerivative middleware_derivative);

  ~BackendTpm1() override;

  void set_middleware_derivative_for_test(
      MiddlewareDerivative middleware_derivative) {
    middleware_derivative_ = middleware_derivative;
  }

 private:
  // This structure holds all Overalls client objects.
  struct OverallsContext {
    overalls::Overalls& overalls;
  };
  struct TssTpmContext {
    TSS_HCONTEXT context;
    TSS_HTPM tpm_handle;
  };

  StatusOr<ScopedTssContext> GetScopedTssContext();
  StatusOr<TssTpmContext> GetTssUserContext();

  State* GetState() override { return &state_; }
  DAMitigation* GetDAMitigation() override { return nullptr; }
  Storage* GetStorage() override { return nullptr; }
  RoData* GetRoData() override { return nullptr; }
  Sealing* GetSealing() override { return &sealing_; }
  SignatureSealing* GetSignatureSealing() override { return nullptr; }
  Deriving* GetDeriving() override { return &deriving_; }
  Encryption* GetEncryption() override { return nullptr; }
  Signing* GetSigning() override { return nullptr; }
  KeyManagerment* GetKeyManagerment() override { return &key_managerment_; }
  SessionManagerment* GetSessionManagerment() override { return nullptr; }
  Config* GetConfig() override { return &config_; }
  Random* GetRandom() override { return &random_; }
  PinWeaver* GetPinWeaver() override { return nullptr; }
  Vendor* GetVendor() override { return nullptr; }

  Proxy& proxy_;

  OverallsContext overall_context_;

  ScopedTssContext tss_user_context_;
  std::optional<TssTpmContext> tss_user_context_cache_;

  StateTpm1 state_{*this};
  SealingTpm1 sealing_{*this};
  DerivingTpm1 deriving_{*this};
  KeyManagermentTpm1 key_managerment_{*this};
  ConfigTpm1 config_{*this};
  RandomTpm1 random_{*this};

  MiddlewareDerivative middleware_derivative_;

  FRIEND_TEST_ALL_PREFIXES(BackendTpm1Test, GetScopedTssContext);
  FRIEND_TEST_ALL_PREFIXES(BackendTpm1Test, GetTssUserContext);
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_BACKEND_H_
