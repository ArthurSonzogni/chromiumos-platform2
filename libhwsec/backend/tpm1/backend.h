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
#include "libhwsec/backend/tpm1/key_management.h"
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

  class DAMitigationTpm1 : public DAMitigation,
                           public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<bool> IsReady() override;
    StatusOr<DAMitigationStatus> GetStatus() override;
    Status Mitigate() override;
  };

  class StorageTpm1 : public Storage, public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<ReadyState> IsReady(Space space) override;
    Status Prepare(Space space, uint32_t size) override;
    StatusOr<brillo::Blob> Load(Space space) override;
    Status Store(Space space, const brillo::Blob& blob) override;
    Status Lock(Space space, LockOptions options) override;
    Status Destroy(Space space) override;
    StatusOr<bool> IsWriteLocked(Space space) override;
  };

  class SealingTpm1 : public Sealing, public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<bool> IsSupported() override;
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

  class EncryptionTpm1 : public Encryption, public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<brillo::Blob> Encrypt(Key key,
                                   const brillo::SecureBlob& plaintext,
                                   EncryptionOptions options) override;
    StatusOr<brillo::SecureBlob> Decrypt(Key key,
                                         const brillo::Blob& ciphertext,
                                         EncryptionOptions options) override;
  };

  class KeyManagementTpm1 : public KeyManagement,
                            public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;

    ~KeyManagementTpm1();
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

    // Below are TPM1.2 specific code.

    // Gets the reference of the internal key data.
    StatusOr<std::reference_wrapper<KeyTpm1>> GetKeyData(Key key);

    // Creates a key object for the RSA public key, given its public modulus in
    // |key_modulus|, creation flags in |key_flags|, signature scheme or
    // |TSS_SS_NONE| in |signature_scheme|, encryption scheme or |TSS_ES_NONE|
    // in |encryption_scheme|. The key's public exponent is assumed to be 65537.
    StatusOr<ScopedKey> CreateRsaPublicKeyObject(brillo::Blob key_modulus,
                                                 uint32_t key_flags,
                                                 uint32_t signature_scheme,
                                                 uint32_t encryption_scheme);

    // Loads the key from its DER-encoded Subject Public Key Info.
    // Currently, only the RSA signing keys are supported.
    StatusOr<ScopedKey> LoadPublicKeyFromSpki(
        const brillo::Blob& public_key_spki_der,
        uint32_t signature_scheme,
        uint32_t encryption_scheme);

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
    StatusOr<bool> IsCurrentUserSet() override;
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

  class PinWeaverTpm1 : public PinWeaver, public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<bool> IsEnabled() override;
    StatusOr<uint8_t> GetVersion() override;
    StatusOr<CredentialTreeResult> Reset(uint32_t bits_per_level,
                                         uint32_t length_labels) override;
    StatusOr<CredentialTreeResult> InsertCredential(
        const std::vector<OperationPolicySetting>& policies,
        const uint64_t label,
        const std::vector<brillo::Blob>& h_aux,
        const brillo::SecureBlob& le_secret,
        const brillo::SecureBlob& he_secret,
        const brillo::SecureBlob& reset_secret,
        const DelaySchedule& delay_schedule) override;
    StatusOr<CredentialTreeResult> CheckCredential(
        const uint64_t label,
        const std::vector<brillo::Blob>& h_aux,
        const brillo::Blob& orig_cred_metadata,
        const brillo::SecureBlob& le_secret) override;
    StatusOr<CredentialTreeResult> RemoveCredential(
        const uint64_t label,
        const std::vector<std::vector<uint8_t>>& h_aux,
        const std::vector<uint8_t>& mac) override;
    StatusOr<CredentialTreeResult> ResetCredential(
        const uint64_t label,
        const std::vector<std::vector<uint8_t>>& h_aux,
        const std::vector<uint8_t>& orig_cred_metadata,
        const brillo::SecureBlob& reset_secret) override;
    StatusOr<GetLogResult> GetLog(
        const std::vector<uint8_t>& cur_disk_root_hash) override;
    StatusOr<ReplayLogOperationResult> ReplayLogOperation(
        const brillo::Blob& log_entry_root,
        const std::vector<brillo::Blob>& h_aux,
        const brillo::Blob& orig_cred_metadata) override;
    StatusOr<int> GetWrongAuthAttempts(
        const brillo::Blob& cred_metadata) override;
    StatusOr<DelaySchedule> GetDelaySchedule(
        const brillo::Blob& cred_metadata) override;
    StatusOr<uint32_t> GetDelayInSeconds(
        const brillo::Blob& cred_metadata) override;
  };

  class VendorTpm1 : public Vendor, public SubClassHelper<BackendTpm1> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<uint32_t> GetFamily() override;
    StatusOr<uint64_t> GetSpecLevel() override;
    StatusOr<uint32_t> GetManufacturer() override;
    StatusOr<uint32_t> GetTpmModel() override;
    StatusOr<uint64_t> GetFirmwareVersion() override;
    StatusOr<brillo::Blob> GetVendorSpecific() override;
    StatusOr<int32_t> GetFingerprint() override;
    StatusOr<bool> IsSrkRocaVulnerable() override;
    StatusOr<brillo::Blob> GetRsuDeviceId() override;
    StatusOr<brillo::Blob> GetIFXFieldUpgradeInfo() override;
    Status DeclareTpmFirmwareStable() override;
    StatusOr<brillo::Blob> SendRawCommand(const brillo::Blob& command) override;

   private:
    Status EnsureVersionInfo();

    std::optional<tpm_manager::GetVersionInfoReply> version_info_;
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

  StatusOr<ScopedTssContext> GetScopedTssContext();
  StatusOr<TSS_HCONTEXT> GetTssContext();
  StatusOr<TSS_HTPM> GetUserTpmHandle();

  // The delegate TPM handle would not be cached to prevent leaking the delegate
  // permission.
  StatusOr<ScopedTssObject<TSS_HTPM>> GetDelegateTpmHandle();

  State* GetState() override { return &state_; }
  DAMitigation* GetDAMitigation() override { return &da_mitigation_; }
  Storage* GetStorage() override { return &storage_; }
  RoData* GetRoData() override { return nullptr; }
  Sealing* GetSealing() override { return &sealing_; }
  SignatureSealing* GetSignatureSealing() override { return nullptr; }
  Deriving* GetDeriving() override { return &deriving_; }
  Encryption* GetEncryption() override { return &encryption_; }
  Signing* GetSigning() override { return nullptr; }
  KeyManagement* GetKeyManagement() override { return &key_management_; }
  SessionManagement* GetSessionManagement() override { return nullptr; }
  Config* GetConfig() override { return &config_; }
  Random* GetRandom() override { return &random_; }
  PinWeaver* GetPinWeaver() override { return &pinweaver_; }
  Vendor* GetVendor() override { return &vendor_; }

  Proxy& proxy_;

  OverallsContext overall_context_;

  std::optional<ScopedTssContext> tss_context_;
  std::optional<ScopedTssObject<TSS_HTPM>> user_tpm_handle_;

  StateTpm1 state_{*this};
  DAMitigationTpm1 da_mitigation_{*this};
  StorageTpm1 storage_{*this};
  SealingTpm1 sealing_{*this};
  DerivingTpm1 deriving_{*this};
  EncryptionTpm1 encryption_{*this};
  KeyManagementTpm1 key_management_{*this};
  ConfigTpm1 config_{*this};
  RandomTpm1 random_{*this};
  PinWeaverTpm1 pinweaver_{*this};
  VendorTpm1 vendor_{*this};

  MiddlewareDerivative middleware_derivative_;

  FRIEND_TEST_ALL_PREFIXES(BackendTpm1Test, GetScopedTssContext);
  FRIEND_TEST_ALL_PREFIXES(BackendTpm1Test, GetTssContext);
  FRIEND_TEST_ALL_PREFIXES(BackendTpm1Test, GetUserTpmHandle);
  FRIEND_TEST_ALL_PREFIXES(BackendTpm1Test, GetDelegateTpmHandle);

  friend class BackendTpm1TestBase;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM1_BACKEND_H_
