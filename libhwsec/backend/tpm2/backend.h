// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_BACKEND_H_
#define LIBHWSEC_BACKEND_TPM2_BACKEND_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <trunks/command_transceiver.h>
#include <trunks/trunks_factory.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/backend/tpm2/key_management.h"
#include "libhwsec/middleware/middleware.h"
#include "libhwsec/proxy/proxy.h"

namespace hwsec {

class BackendTpm2 : public Backend {
 public:
  class StateTpm2 : public State, public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<bool> IsEnabled() override;
    StatusOr<bool> IsReady() override;
    Status Prepare() override;
  };

  class DAMitigationTpm2 : public DAMitigation,
                           public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<bool> IsReady() override;
    StatusOr<DAMitigationStatus> GetStatus() override;
    Status Mitigate() override;
  };

  class StorageTpm2 : public Storage, public SubClassHelper<BackendTpm2> {
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

  class SealingTpm2 : public Sealing, public SubClassHelper<BackendTpm2> {
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
  };

  class DerivingTpm2 : public Deriving, public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<brillo::Blob> Derive(Key key, const brillo::Blob& blob) override;
    StatusOr<brillo::SecureBlob> SecureDerive(
        Key key, const brillo::SecureBlob& blob) override;

   private:
    StatusOr<brillo::SecureBlob> DeriveRsaKey(const KeyTpm2& key_data,
                                              const brillo::SecureBlob& blob);
    StatusOr<brillo::SecureBlob> DeriveEccKey(const KeyTpm2& key_data,
                                              const brillo::SecureBlob& blob);
  };

  class EncryptionTpm2 : public Encryption, public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<brillo::Blob> Encrypt(Key key,
                                   const brillo::SecureBlob& plaintext,
                                   EncryptionOptions options) override;
    StatusOr<brillo::SecureBlob> Decrypt(Key key,
                                         const brillo::Blob& ciphertext,
                                         EncryptionOptions options) override;
  };

  class KeyManagementTpm2 : public KeyManagement,
                            public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    ~KeyManagementTpm2();

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
                                           bool auto_reload);
    StatusOr<CreateKeyResult> CreateSoftwareGenRsaKey(
        const OperationPolicySetting& policy,
        const CreateKeyOptions& options,
        bool auto_reload);
    StatusOr<CreateKeyResult> CreateEccKey(const OperationPolicySetting& policy,
                                           const CreateKeyOptions& options,
                                           bool auto_reload);
    StatusOr<ScopedKey> LoadKeyInternal(
        KeyTpm2::Type key_type,
        uint32_t key_handle,
        std::optional<KeyReloadDataTpm2> reload_data);

    KeyToken current_token_ = 0;
    absl::flat_hash_map<KeyToken, KeyTpm2> key_map_;
    absl::flat_hash_map<PersistentKeyType, KeyToken> persistent_key_map_;
  };

  class ConfigTpm2 : public Config, public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<OperationPolicy> ToOperationPolicy(
        const OperationPolicySetting& policy) override;
    Status SetCurrentUser(const std::string& current_user) override;
    StatusOr<bool> IsCurrentUserSet() override;
    StatusOr<QuoteResult> Quote(DeviceConfigs device_config, Key key) override;

    using PcrMap = std::map<uint32_t, std::string>;
    struct TrunksSession {
      using InnerSession = std::variant<std::unique_ptr<trunks::HmacSession>,
                                        std::unique_ptr<trunks::PolicySession>>;
      InnerSession session;
      trunks::AuthorizationDelegate* delegate;
    };

    // Defines a set of PCR indexes (in bitmask) and the digest that is valid
    // after computation of sha256 of concatenation of PCR values included in
    // bitmask.
    struct PcrValue {
      // The set of PCR indexes that have to pass the validation.
      uint8_t bitmask[2];
      // The hash digest of the PCR values contained in the bitmask.
      std::string digest;
    };

    // Converts a device config usage into a PCR map.
    StatusOr<PcrMap> ToPcrMap(const DeviceConfigs& device_config);

    // Converts a device config setting into a PCR map.
    StatusOr<PcrMap> ToSettingsPcrMap(const DeviceConfigSettings& settings);

    // Creates a trunks policy session from |policy|, and PolicyOR the
    // |extra_policy_digests| if it's not empty.
    StatusOr<std::unique_ptr<trunks::PolicySession>> GetTrunksPolicySession(
        const OperationPolicy& policy,
        const std::vector<std::string>& extra_policy_digests,
        bool salted,
        bool enable_encryption);

    // Creates a unified session from |policy|.
    StatusOr<TrunksSession> GetTrunksSession(const OperationPolicy& policy,
                                             bool salted,
                                             bool enable_encryption);

    // Creates the PCR value for PinWeaver digest.
    StatusOr<PcrValue> ToPcrValue(const DeviceConfigSettings& settings);

    // Creates the policy digest from device config setting.
    StatusOr<std::string> ToPolicyDigest(const DeviceConfigSettings& settings);

   private:
    StatusOr<std::string> ReadPcr(uint32_t pcr_index);
  };

  class RandomTpm2 : public Random, public SubClassHelper<BackendTpm2> {
   public:
    using SubClassHelper::SubClassHelper;
    StatusOr<brillo::Blob> RandomBlob(size_t size) override;
    StatusOr<brillo::SecureBlob> RandomSecureBlob(size_t size) override;
  };

  class PinWeaverTpm2 : public PinWeaver, public SubClassHelper<BackendTpm2> {
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

   private:
    // The protocol version used by pinweaver.
    std::optional<uint8_t> protocol_version_;
  };

  class VendorTpm2 : public Vendor, public SubClassHelper<BackendTpm2> {
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

    bool fw_declared_stable_ = false;
    std::optional<tpm_manager::GetVersionInfoReply> version_info_;
  };

  BackendTpm2(Proxy& proxy, MiddlewareDerivative middleware_derivative);

  ~BackendTpm2() override;

  void set_middleware_derivative_for_test(
      MiddlewareDerivative middleware_derivative) {
    middleware_derivative_ = middleware_derivative;
  }

 private:
  // This structure holds all Trunks client objects.
  struct TrunksClientContext {
    trunks::CommandTransceiver& command_transceiver;
    trunks::TrunksFactory& factory;
    std::unique_ptr<trunks::TpmState> tpm_state;
    std::unique_ptr<trunks::TpmUtility> tpm_utility;
  };

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

  TrunksClientContext trunks_context_;

  StateTpm2 state_{*this};
  DAMitigationTpm2 da_mitigation_{*this};
  StorageTpm2 storage_{*this};
  SealingTpm2 sealing_{*this};
  DerivingTpm2 deriving_{*this};
  EncryptionTpm2 encryption_{*this};
  KeyManagementTpm2 key_management_{*this};
  ConfigTpm2 config_{*this};
  RandomTpm2 random_{*this};
  PinWeaverTpm2 pinweaver_{*this};
  VendorTpm2 vendor_{*this};

  MiddlewareDerivative middleware_derivative_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_BACKEND_H_
