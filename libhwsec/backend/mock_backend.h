// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_MOCK_BACKEND_H_
#define LIBHWSEC_BACKEND_MOCK_BACKEND_H_

#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <absl/container/flat_hash_set.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "libhwsec/backend/backend.h"

namespace hwsec {

class MockBackend : public Backend {
 public:
  class MockState : public State {
   public:
    MOCK_METHOD(StatusOr<bool>, IsEnabled, (), (override));
    MOCK_METHOD(StatusOr<bool>, IsReady, (), (override));
    MOCK_METHOD(Status, Prepare, (), (override));
  };

  class MockDAMitigation : public DAMitigation {
   public:
    MOCK_METHOD(StatusOr<bool>, IsReady, (), (override));
    MOCK_METHOD(StatusOr<DAMitigationStatus>, GetStatus, (), (override));
    MOCK_METHOD(Status, Mitigate, (), (override));
  };

  class MockStorage : public Storage {
   public:
    MOCK_METHOD(StatusOr<ReadyState>, IsReady, (Space space), (override));
    MOCK_METHOD(Status, Prepare, (Space space, uint32_t size), (override));
    MOCK_METHOD(StatusOr<brillo::Blob>, Load, (Space space), (override));
    MOCK_METHOD(Status,
                Store,
                (Space space, const brillo::Blob& blob),
                (override));
    MOCK_METHOD(Status, Lock, (Space space, LockOptions options), (override));
    MOCK_METHOD(Status, Destroy, (Space space), (override));
    MOCK_METHOD(StatusOr<bool>, IsWriteLocked, (Space space), (override));
  };

  class MockRoData : public RoData {
   public:
    MOCK_METHOD(StatusOr<bool>, IsReady, (RoSpace space), (override));
    MOCK_METHOD(StatusOr<brillo::Blob>, Read, (RoSpace space), (override));
    MOCK_METHOD(StatusOr<brillo::Blob>,
                Certify,
                (RoSpace space, Key key),
                (override));
  };

  class MockSealing : public Sealing {
   public:
    MOCK_METHOD(StatusOr<bool>, IsSupported, (), (override));
    MOCK_METHOD(StatusOr<brillo::Blob>,
                Seal,
                (const OperationPolicySetting& policy,
                 const brillo::SecureBlob& unsealed_data),
                (override));
    MOCK_METHOD(StatusOr<std::optional<ScopedKey>>,
                PreloadSealedData,
                (const OperationPolicy& policy,
                 const brillo::Blob& sealed_data),
                (override));
    MOCK_METHOD(StatusOr<brillo::SecureBlob>,
                Unseal,
                (const OperationPolicy& policy,
                 const brillo::Blob& sealed_data,
                 UnsealOptions options),
                (override));
  };

  class MockSignatureSealing : public SignatureSealing {
   public:
    MOCK_METHOD(StatusOr<SealedData>,
                Seal,
                (const OperationPolicySetting& policy,
                 const brillo::SecureBlob& unsealed_data,
                 const brillo::Blob& public_key_spki_der,
                 const std::vector<Algorithm>& key_algorithms),
                (override));
    MOCK_METHOD(StatusOr<ChallengeResult>,
                Challenge,
                (const OperationPolicy& policy,
                 const SealedData& sealed_data,
                 const brillo::Blob& public_key_spki_der,
                 const std::vector<Algorithm>& key_algorithms),
                (override));
    MOCK_METHOD(StatusOr<brillo::SecureBlob>,
                Unseal,
                (ChallengeID challenge, const brillo::Blob challenge_response),
                (override));
  };

  class MockDeriving : public Deriving {
   public:
    MOCK_METHOD(StatusOr<brillo::Blob>,
                Derive,
                (Key key, const brillo::Blob& blob),
                (override));
    MOCK_METHOD(StatusOr<brillo::SecureBlob>,
                SecureDerive,
                (Key key, const brillo::SecureBlob& blob),
                (override));
  };

  class MockEncryption : public Encryption {
   public:
    MOCK_METHOD(StatusOr<brillo::Blob>,
                Encrypt,
                (Key key,
                 const brillo::SecureBlob& plaintext,
                 EncryptionOptions options),
                (override));
    MOCK_METHOD(StatusOr<brillo::SecureBlob>,
                Decrypt,
                (Key key,
                 const brillo::Blob& ciphertext,
                 EncryptionOptions options),
                (override));
  };

  class MockSigning : public Signing {
   public:
    MOCK_METHOD(StatusOr<brillo::Blob>,
                Sign,
                (const OperationPolicy& policy,
                 Key key,
                 const brillo::Blob& data),
                (override));
    MOCK_METHOD(Status,
                Verify,
                (const OperationPolicy& policy,
                 Key key,
                 const brillo::Blob& signed_data),
                (override));
  };

  class MockKeyManagerment : public KeyManagerment {
   public:
    MOCK_METHOD(StatusOr<absl::flat_hash_set<KeyAlgoType>>,
                GetSupportedAlgo,
                (),
                (override));
    MOCK_METHOD(StatusOr<CreateKeyResult>,
                CreateKey,
                (const OperationPolicySetting& policy,
                 KeyAlgoType key_algo,
                 CreateKeyOptions options),
                (override));
    MOCK_METHOD(StatusOr<ScopedKey>,
                LoadKey,
                (const OperationPolicy& policy, const brillo::Blob& key_blob),
                (override));
    MOCK_METHOD(StatusOr<CreateKeyResult>,
                CreateAutoReloadKey,
                (const OperationPolicySetting& policy,
                 KeyAlgoType key_algo,
                 CreateKeyOptions options),
                (override));
    MOCK_METHOD(StatusOr<ScopedKey>,
                LoadAutoReloadKey,
                (const OperationPolicy& policy, const brillo::Blob& key_blob),
                (override));
    MOCK_METHOD(StatusOr<ScopedKey>,
                GetPersistentKey,
                (PersistentKeyType key_type),
                (override));
    MOCK_METHOD(StatusOr<brillo::Blob>, GetPubkeyHash, (Key key), (override));
    MOCK_METHOD(Status, Flush, (Key key), (override));
    MOCK_METHOD(Status, ReloadIfPossible, (Key key), (override));
    MOCK_METHOD(StatusOr<ScopedKey>,
                SideLoadKey,
                (uint32_t key_handle),
                (override));
    MOCK_METHOD(StatusOr<uint32_t>, GetKeyHandle, (Key key), (override));
  };

  class MockSessionManagerment : public SessionManagerment {
   public:
    MOCK_METHOD(StatusOr<ScopedSession>,
                CreateSession,
                (const OperationPolicy& policy, CreateSessionOptions options),
                (override));
    MOCK_METHOD(Status, Flush, (Session session), (override));
    MOCK_METHOD(StatusOr<ScopedSession>,
                SideLoadSession,
                (uint32_t session_handle),
                (override));
  };

  class MockConfig : public Config {
   public:
    MOCK_METHOD(StatusOr<OperationPolicy>,
                ToOperationPolicy,
                (const OperationPolicySetting& policy),
                (override));
    MOCK_METHOD(Status,
                SetCurrentUser,
                (const std::string& current_user),
                (override));
    MOCK_METHOD(StatusOr<QuoteResult>,
                Quote,
                (DeviceConfigs device_config, Key key),
                (override));
  };

  class MockRandom : public Random {
   public:
    MOCK_METHOD(StatusOr<brillo::Blob>, RandomBlob, (size_t size), (override));
    MOCK_METHOD(StatusOr<brillo::SecureBlob>,
                RandomSecureBlob,
                (size_t size),
                (override));
  };

  class MockPinWeaver : public PinWeaver {
   public:
    MOCK_METHOD(StatusOr<bool>, IsEnabled, (), (override));
    MOCK_METHOD(StatusOr<uint8_t>, GetVersion, (), (override));
    MOCK_METHOD(StatusOr<CredentialTreeResult>,
                Reset,
                (uint32_t bits_per_level, uint32_t length_labels),
                (override));
    MOCK_METHOD(StatusOr<CredentialTreeResult>,
                InsertCredential,
                (const std::vector<OperationPolicySetting>& policies,
                 const uint64_t label,
                 const std::vector<brillo::Blob>& h_aux,
                 const brillo::SecureBlob& le_secret,
                 const brillo::SecureBlob& he_secret,
                 const brillo::SecureBlob& reset_secret,
                 const DelaySchedule& delay_schedule),
                (override));
    MOCK_METHOD(StatusOr<CredentialTreeResult>,
                CheckCredential,
                (const uint64_t label,
                 const std::vector<brillo::Blob>& h_aux,
                 const brillo::Blob& orig_cred_metadata,
                 const brillo::SecureBlob& le_secret),
                (override));
    MOCK_METHOD(StatusOr<CredentialTreeResult>,
                RemoveCredential,
                (const uint64_t label,
                 const std::vector<std::vector<uint8_t>>& h_aux,
                 const std::vector<uint8_t>& mac),
                (override));
    MOCK_METHOD(StatusOr<CredentialTreeResult>,
                ResetCredential,
                (const uint64_t label,
                 const std::vector<std::vector<uint8_t>>& h_aux,
                 const std::vector<uint8_t>& orig_cred_metadata,
                 const brillo::SecureBlob& reset_secret),
                (override));
    MOCK_METHOD(StatusOr<GetLogResult>,
                GetLog,
                (const std::vector<uint8_t>& cur_disk_root_hash),
                (override));
    MOCK_METHOD(StatusOr<ReplayLogOperationResult>,
                ReplayLogOperation,
                (const brillo::Blob& log_entry_root,
                 const std::vector<brillo::Blob>& h_aux,
                 const brillo::Blob& orig_cred_metadata),
                (override));
    MOCK_METHOD(StatusOr<int>,
                GetWrongAuthAttempts,
                (const brillo::Blob& cred_metadata),
                (override));
  };

  class MockVendor : public Vendor {
   public:
    MOCK_METHOD(StatusOr<uint32_t>, GetFamily, (), (override));
    MOCK_METHOD(StatusOr<uint64_t>, GetSpecLevel, (), (override));
    MOCK_METHOD(StatusOr<uint32_t>, GetManufacturer, (), (override));
    MOCK_METHOD(StatusOr<uint32_t>, GetTpmModel, (), (override));
    MOCK_METHOD(StatusOr<uint64_t>, GetFirmwareVersion, (), (override));
    MOCK_METHOD(StatusOr<brillo::Blob>, GetVendorSpecific, (), (override));
    MOCK_METHOD(StatusOr<int32_t>, GetFingerprint, (), (override));
    MOCK_METHOD(StatusOr<bool>, IsSrkRocaVulnerable, (), (override));
    MOCK_METHOD(StatusOr<brillo::Blob>, GetIFXFieldUpgradeInfo, (), (override));
    MOCK_METHOD(Status, DeclareTpmFirmwareStable, (), (override));
    MOCK_METHOD(StatusOr<brillo::Blob>,
                SendRawCommand,
                (const brillo::Blob& command),
                (override));
  };

  struct MockBackendData {
    MockState state;
    MockDAMitigation da_mitigation;
    MockStorage storage;
    MockRoData ro_data;
    MockSealing sealing;
    MockSignatureSealing signature_sealing;
    MockDeriving deriving;
    MockEncryption encryption;
    MockSigning signing;
    MockKeyManagerment key_managerment;
    MockSessionManagerment session_managerment;
    MockConfig config;
    MockRandom random;
    MockPinWeaver pinweaver;
    MockVendor vendor;
  };

  MockBackend() = default;
  virtual ~MockBackend() = default;

  MockBackendData& GetMock() { return mock_data_; }

 private:
  State* GetState() override { return &mock_data_.state; }
  DAMitigation* GetDAMitigation() override { return &mock_data_.da_mitigation; }
  Storage* GetStorage() override { return &mock_data_.storage; }
  RoData* GetRoData() override { return &mock_data_.ro_data; }
  Sealing* GetSealing() override { return &mock_data_.sealing; }
  SignatureSealing* GetSignatureSealing() override {
    return &mock_data_.signature_sealing;
  }
  Deriving* GetDeriving() override { return &mock_data_.deriving; }
  Encryption* GetEncryption() override { return &mock_data_.encryption; }
  Signing* GetSigning() override { return &mock_data_.signing; }
  KeyManagerment* GetKeyManagerment() override {
    return &mock_data_.key_managerment;
  }
  SessionManagerment* GetSessionManagerment() override {
    return &mock_data_.session_managerment;
  }
  Config* GetConfig() override { return &mock_data_.config; }
  Random* GetRandom() override { return &mock_data_.random; }
  PinWeaver* GetPinWeaver() override { return &mock_data_.pinweaver; }
  Vendor* GetVendor() override { return &mock_data_.vendor; }

  MockBackendData mock_data_;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_MOCK_BACKEND_H_
