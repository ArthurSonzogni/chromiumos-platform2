// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_BACKEND_H_
#define LIBHWSEC_BACKEND_BACKEND_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <absl/container/flat_hash_set.h>
#include <base/notreached.h>
#include <base/time/time.h>
#include <brillo/secure_blob.h>

#include "libhwsec/status.h"
#include "libhwsec/structures/device_config.h"
#include "libhwsec/structures/key.h"
#include "libhwsec/structures/operation_policy.h"
#include "libhwsec/structures/permission.h"
#include "libhwsec/structures/session.h"
#include "libhwsec/structures/space.h"

namespace hwsec {

// Backend is the layer to abstract the difference between different security
// module(e.g. TPM1.2, TPM2.0, GSC...). And provide a unified interface. Note:
// This class is NOT thread safe.
class Backend {
 public:
  // State provide the basic state of the security module.
  class State {
   public:
    // Is the security module enabled or not.
    virtual StatusOr<bool> IsEnabled() = 0;

    // Is the security module ready to use or not.
    virtual StatusOr<bool> IsReady() = 0;

    // Tries to make the security module become ready.
    virtual Status Prepare() = 0;

   protected:
    State() = default;
    ~State() = default;
  };

  // DAMitigation provide the functions related to the DA counter mitigation.
  class DAMitigation {
   public:
    struct DAMitigationStatus {
      bool lockout;
      base::TimeDelta remaining;
    };

    // Is DA counter can be mitigated or not.
    virtual StatusOr<bool> IsReady() = 0;

    // Is DA mitigation status.
    virtual StatusOr<DAMitigationStatus> GetStatus() = 0;

    // Tries to mitigate the DA counter.
    virtual Status Mitigate() = 0;

   protected:
    DAMitigation() = default;
    ~DAMitigation() = default;
  };

  // Storage provide the functions for writeable space.
  class Storage {
   public:
    enum class ReadyState {
      // Ready to use.
      kReady,
      // Write locked.
      kWriteLocked,
      // Can be prepared.
      kPreparable,
    };

    struct LockOptions {
      bool read_lock = false;
      bool write_lock = false;
    };

    // Is the |space| ready to use (defined correctly) or not.
    virtual StatusOr<ReadyState> IsReady(Space space) = 0;

    // Tries to make the |space| become ready and have enough |size| to write.
    virtual Status Prepare(Space space, uint32_t size) = 0;

    // Reads the data from the |space|.
    virtual StatusOr<brillo::Blob> Load(Space space) = 0;

    // Writes the |blob| into the |space|.
    virtual Status Store(Space space, const brillo::Blob& blob) = 0;

    // Locks the |space| with some optional |options|.
    virtual Status Lock(Space space, LockOptions options) = 0;

    // Destroys the |space|.
    virtual Status Destroy(Space space) = 0;

    // Is the |space| write locked or not.
    virtual StatusOr<bool> IsWriteLocked(Space space) = 0;

   protected:
    Storage() = default;
    ~Storage() = default;
  };

  // Storage provide the functions for read-only space.
  class RoData {
   public:
    // Is the |space| ready to use (defined correctly) or not.
    virtual StatusOr<bool> IsReady(RoSpace space) = 0;

    // Reads the data from the |space|.
    virtual StatusOr<brillo::Blob> Read(RoSpace space) = 0;

    // Certifies data the |space| with a |key|.
    virtual StatusOr<brillo::Blob> Certify(RoSpace space, Key key) = 0;

   protected:
    RoData() = default;
    ~RoData() = default;
  };

  // Sealing provide the functions to sealing and unsealing with policy.
  class Sealing {
   public:
    struct UnsealOptions {
      // The preload_data returned from |PreloadSealedData|.
      std::optional<Key> preload_data;
    };

    // Is the device supported sealing/unsealing or not.
    virtual StatusOr<bool> IsSupported() = 0;

    // Seals the |unsealed_data| with |policy|.
    virtual StatusOr<brillo::Blob> Seal(
        const OperationPolicySetting& policy,
        const brillo::SecureBlob& unsealed_data) = 0;

    // Preloads the |sealed_data| with |policy|.
    virtual StatusOr<std::optional<ScopedKey>> PreloadSealedData(
        const OperationPolicy& policy, const brillo::Blob& sealed_data) = 0;

    // Unseals the |sealed_data| with |policy| and optional |options|.
    virtual StatusOr<brillo::SecureBlob> Unseal(const OperationPolicy& policy,
                                                const brillo::Blob& sealed_data,
                                                UnsealOptions options) = 0;

   protected:
    Sealing() = default;
    ~Sealing() = default;
  };

  // SignatureSealing provide the functions to sealing and unsealing with policy
  // and signature challenge.
  class SignatureSealing {
   public:
    struct SealedData {
      struct Tpm1Data {
        brillo::Blob public_key_spki_der;
        brillo::Blob srk_wrapped_secret;
        int32_t scheme;
        int32_t hash_alg;
        std::vector<brillo::Blob> policy_digests;
      };
      struct Tpm2Data {
        brillo::Blob public_key_spki_der;
        brillo::Blob srk_wrapped_cmk;
        brillo::Blob cmk_pubkey;
        brillo::Blob cmk_wrapped_auth_data;
        std::vector<brillo::Blob> pcr_bound_secrets;
      };
      std::variant<Tpm1Data, Tpm2Data> sealed_data;
    };

    enum class Algorithm {
      kRsassaPkcs1V15Sha1,
      kRsassaPkcs1V15Sha256,
      kRsassaPkcs1V15Sha384,
      kRsassaPkcs1V15Sha512,
    };

    enum class ChallengeID : uint32_t;

    struct ChallengeResult {
      NoDefault<ChallengeID> challenge_id;
      brillo::Blob challenge;
    };

    // Seals the |unsealed_data| with |policy| and |public_key_spki_der|.
    //
    // |key_algorithms| is the list of signature algorithms supported by the
    // key. Listed in the order of preference (starting from the most
    // preferred); however, the implementation is permitted to ignore this
    // order.
    virtual StatusOr<SealedData> Seal(
        const OperationPolicySetting& policy,
        const brillo::SecureBlob& unsealed_data,
        const brillo::Blob& public_key_spki_der,
        const std::vector<Algorithm>& key_algorithms) = 0;

    // Creates a challenge from the |sealed_data| with |policy|,
    // |public_key_spki_der|, |key_algorithms|.
    virtual StatusOr<ChallengeResult> Challenge(
        const OperationPolicy& policy,
        const SealedData& sealed_data,
        const brillo::Blob& public_key_spki_der,
        const std::vector<Algorithm>& key_algorithms) = 0;

    // Unseals the sealed_data from previous |challenge| with the
    // |challenge_response|.
    virtual StatusOr<brillo::SecureBlob> Unseal(
        ChallengeID challenge, const brillo::Blob challenge_response) = 0;

   protected:
    SignatureSealing() = default;
    ~SignatureSealing() = default;
  };

  // Deriving provide the functions to derive blob.
  class Deriving {
   public:
    // Derives the |blob| with |key|.
    // Note: The function may return same |blob| on some platform(e.g. TPM1.2)
    // for backward compatibility.
    virtual StatusOr<brillo::Blob> Derive(Key key,
                                          const brillo::Blob& blob) = 0;

    // Derives the secure |blob| with |key|.
    // Note: The function may return same |blob| on some platform(e.g. TPM1.2)
    // for backward compatibility.
    virtual StatusOr<brillo::SecureBlob> SecureDerive(
        Key key, const brillo::SecureBlob& blob) = 0;

   protected:
    Deriving() = default;
    ~Deriving() = default;
  };

  // Encryption provide the functions to encrypt and decrypt blob.
  class Encryption {
   public:
    struct EncryptionOptions {
      enum class Schema {
        kDefault,
        kNull,
      };
      Schema schema = Schema::kDefault;
    };

    // Encrypts the |plaintext| with |key| and optional |options|.
    virtual StatusOr<brillo::Blob> Encrypt(Key key,
                                           const brillo::SecureBlob& plaintext,
                                           EncryptionOptions options) = 0;

    // Decrypts the |ciphertext| with |key| and optional |options|.
    virtual StatusOr<brillo::SecureBlob> Decrypt(Key key,
                                                 const brillo::Blob& ciphertext,
                                                 EncryptionOptions options) = 0;

   protected:
    Encryption() = default;
    ~Encryption() = default;
  };

  // Signing provide the functions to sign and verify.
  class Signing {
   public:
    // Signs the |data| with |policy| and |key|.
    virtual StatusOr<brillo::Blob> Sign(const OperationPolicy& policy,
                                        Key key,
                                        const brillo::Blob& data) = 0;

    // Verifies the |signed_data| with |policy| and |key|.
    virtual Status Verify(const OperationPolicy& policy,
                          Key key,
                          const brillo::Blob& signed_data) = 0;

   protected:
    Signing() = default;
    ~Signing() = default;
  };

  // KeyManagement provide the functions to manager key.
  class KeyManagement {
   public:
    enum class PersistentKeyType {
      kStorageRootKey,
    };
    struct CreateKeyOptions {
      bool allow_software_gen = false;
      bool allow_decrypt = false;
      bool allow_sign = false;
    };
    struct CreateKeyResult {
      ScopedKey key;
      brillo::Blob key_blob;
    };

    // Gets the supported algorithm.
    virtual StatusOr<absl::flat_hash_set<KeyAlgoType>> GetSupportedAlgo() = 0;

    // Creates a key with |key_algo| algorithm, |policy| and optional |options|.
    virtual StatusOr<CreateKeyResult> CreateKey(
        const OperationPolicySetting& policy,
        KeyAlgoType key_algo,
        CreateKeyOptions options) = 0;

    // Loads a key from |key_blob| with |policy|.
    virtual StatusOr<ScopedKey> LoadKey(const OperationPolicy& policy,
                                        const brillo::Blob& key_blob) = 0;

    // Creates an auto-reload key with |key_algo| algorithm, |policy| and
    // optional |options|.
    virtual StatusOr<CreateKeyResult> CreateAutoReloadKey(
        const OperationPolicySetting& policy,
        KeyAlgoType key_algo,
        CreateKeyOptions options) = 0;

    // Loads an auto-reload key from |key_blob| with |policy|.
    virtual StatusOr<ScopedKey> LoadAutoReloadKey(
        const OperationPolicy& policy, const brillo::Blob& key_blob) = 0;

    // Loads the persistent key with specific |key_type|.
    virtual StatusOr<ScopedKey> GetPersistentKey(
        PersistentKeyType key_type) = 0;

    // Loads the hash of public part of the |key|.
    virtual StatusOr<brillo::Blob> GetPubkeyHash(Key key) = 0;

    // Flushes the |key| to reclaim the resource.
    virtual Status Flush(Key key) = 0;

    // Reloads the |key| if possible.
    virtual Status ReloadIfPossible(Key key) = 0;

    // Loads the key with raw |key_handle|.
    // TODO(174816474): deprecated legacy APIs.
    virtual StatusOr<ScopedKey> SideLoadKey(uint32_t key_handle) = 0;

    // Loads the raw |key_handle| from key.
    // TODO(174816474): deprecated legacy APIs.
    virtual StatusOr<uint32_t> GetKeyHandle(Key key) = 0;

   protected:
    KeyManagement() = default;
    ~KeyManagement() = default;
  };

  // KeyManagement provide the functions to manager session.
  class SessionManagement {
   public:
    struct CreateSessionOptions {};

    // Creates a session with |policy| and optional |options|.
    virtual StatusOr<ScopedSession> CreateSession(
        const OperationPolicy& policy, CreateSessionOptions options) = 0;

    // Flushes the |session| to reclaim the resource.
    virtual Status Flush(Session session) = 0;

    // Loads the session with raw |session_handle|.
    // TODO(174816474): deprecated legacy APIs.
    virtual StatusOr<ScopedSession> SideLoadSession(
        uint32_t session_handle) = 0;

   protected:
    SessionManagement() = default;
    ~SessionManagement() = default;
  };

  // Config provide the functions to change settings and policies.
  class Config {
   public:
    struct QuoteResult {
      brillo::Blob unquoted_device_config;
      brillo::Blob quoted_data;
      brillo::Blob signature;
    };

    // Converts the operation |policy| setting to operation policy.
    virtual StatusOr<OperationPolicy> ToOperationPolicy(
        const OperationPolicySetting& policy) = 0;

    // Sets the |current_user| config.
    virtual Status SetCurrentUser(const std::string& current_user) = 0;

    // Quotes the |device_config| with |key|.
    virtual StatusOr<QuoteResult> Quote(DeviceConfigs device_config,
                                        Key key) = 0;

   protected:
    Config() = default;
    ~Config() = default;
  };

  // Random provide the functions to generate random.
  class Random {
   public:
    // Generates random blob with |size|.
    virtual StatusOr<brillo::Blob> RandomBlob(size_t size) = 0;

    // Generates random secure blob with |size|.
    virtual StatusOr<brillo::SecureBlob> RandomSecureBlob(size_t size) = 0;

   protected:
    Random() = default;
    ~Random() = default;
  };

  // Random provide the functions related to pinweaver.
  class PinWeaver {
   public:
    // The result object for those operation that will modify the hash tree.
    // If an operation return this struct, the tree should be updated to
    // |new_root|, otherwise the tree would out of sync.
    struct CredentialTreeResult {
      enum class ErrorCode {
        // The operation was successful.
        kSuccess = 0,
        // Failed due to incorrect Low Entropy credential provided.
        kInvalidLeSecret,
        // Failed due to incorrect Reset credential provided.
        kInvalidResetSecret,
        // Failed since the credential has been locked out due to too many
        // attempts per the delay schedule.
        kTooManyAttempts,
        // Failed due to the hash tree being out of sync. This should
        // prompt a hash tree resynchronization and retry.
        kHashTreeOutOfSync,
        // The device policy (e.g. PCR) is in unexpected state. The basic way to
        // proceed from here is to
        // reboot the device.
        kPolicyNotMatch,
        // Unknown error.
        kUnknown,
      };

      // Detail error code for more information.
      // Whatever the error code we return at here, we should always update the
      // tree with |new_root|.
      NoDefault<ErrorCode> error;
      // The |new_root| hash of the hash tree.
      NoDefault<brillo::Blob> new_root;
      // The credential metadata that should be updated.
      std::optional<brillo::Blob> new_cred_metadata;
      // The MAC of the credential that should be updated.
      std::optional<brillo::Blob> new_mac;
      // The high entropy secret that would be returned in a successful check
      // operation.
      std::optional<brillo::SecureBlob> he_secret;
      // The reset secret that would be returned in a successful check
      // operation.
      std::optional<brillo::SecureBlob> reset_secret;
    };

    struct GetLogResult {
      // Enum used to denote the log entry type.
      enum class LogEntryType {
        kInsert,
        kCheck,
        kRemove,
        kReset,
        kInvalid,
      };

      // Container for the log replay data obtained from the backend.
      // This struct is used during synchronization operations which occur when
      // the on-disk hash tree state and backend hash tree state are
      // out-of-sync.
      struct LogEntry {
        LogEntryType type;
        // Label on which the log operation is performed.
        uint64_t label;
        // Value of root hash after the log operation is performed.
        brillo::Blob root;
        // For insert operations, this signifies the MAC of the inserted leaf.
        brillo::Blob mac;
      };

      brillo::Blob root_hash;
      std::vector<LogEntry> log_entries;
    };

    struct ReplayLogOperationResult {
      brillo::Blob new_cred_metadata;
      brillo::Blob new_mac;
    };

    // The delay schedule which determines the delay enforced between
    // authentication attempts.
    using DelaySchedule = std::map<uint32_t, uint32_t>;

    // Is the pinweaver enabled or not.
    virtual StatusOr<bool> IsEnabled() = 0;

    // Gets the version of pinweaver.
    virtual StatusOr<uint8_t> GetVersion() = 0;

    // Resets the PinWeaver Hash Tree root hash with its initial known value,
    // which assumes all MACs are all-zero.
    //
    // This function should be executed only when we are setting up a hash tree
    // on a new / wiped device, or resetting the hash tree due to an
    // unrecoverable error.
    //
    // |bits_per_level| is the number of bits per level of the hash tree.
    // |length_labels| is the length of the leaf bit string.
    //
    // In all cases, the resulting root hash is returned in |new_root|.
    virtual StatusOr<CredentialTreeResult> Reset(uint32_t bits_per_level,
                                                 uint32_t length_labels) = 0;

    // Tries to insert a credential into the TPM.
    //
    // The label of the leaf node is in |label|, the list of auxiliary hashes is
    // in |h_aux|, the LE credential to be added is in |le_secret|. Along with
    // it, its associated reset_secret |reset_secret| and the high entropy
    // credential it protects |he_secret| are also provided. The delay schedule
    // which determines the delay enforced between authentication attempts is
    // provided by |delay_schedule|. And the credential is bound to the the
    // |policies|, the check credential operation would only success when one of
    // policy match.
    //
    // |h_aux| requires a particular order: starting from left child to right
    // child, from leaf upwards till the children of the root label.
    //
    // If successful, the new credential metadata will be placed in the blob
    // pointed by |new_cred_metadata|. The MAC of the credential will be
    // returned in |new_mac|. The resulting root hash is returned in |new_root|.
    //
    // In all cases, the resulting root hash is returned in |new_root|.
    virtual StatusOr<CredentialTreeResult> InsertCredential(
        const std::vector<OperationPolicySetting>& policies,
        const uint64_t label,
        const std::vector<brillo::Blob>& h_aux,
        const brillo::SecureBlob& le_secret,
        const brillo::SecureBlob& he_secret,
        const brillo::SecureBlob& reset_secret,
        const DelaySchedule& delay_schedule) = 0;

    // Tries to verify/authenticate a credential.
    //
    // The obfuscated LE Credential is |le_secret| and the credential metadata
    // is in |orig_cred_metadata|.
    //
    // On success, or failure due to an invalid |le_secret|, the updated
    // credential metadata and corresponding new MAC will be returned in
    // |new_cred_metadata| and |new_mac|.
    //
    // On success, the released high entropy credential will be returned in
    // |he_secret| and the reset secret in |reset_secret|.
    //
    // In all cases, the resulting root hash is returned in |new_root|.
    virtual StatusOr<CredentialTreeResult> CheckCredential(
        const uint64_t label,
        const std::vector<brillo::Blob>& h_aux,
        const brillo::Blob& orig_cred_metadata,
        const brillo::SecureBlob& le_secret) = 0;

    // Removes the credential which has label |label|.
    //
    // The corresponding list of auxiliary hashes is in |h_aux|, and the MAC of
    // the label that needs to be removed is |mac|.
    //
    // In all cases, the resulting root hash is returned in |new_root|.
    virtual StatusOr<CredentialTreeResult> RemoveCredential(
        const uint64_t label,
        const std::vector<std::vector<uint8_t>>& h_aux,
        const std::vector<uint8_t>& mac) = 0;

    // Tries to reset a (potentially locked out) credential.
    //
    // The reset credential is |reset_secret| and the credential metadata is
    // in |orig_cred_metadata|.
    //
    // On success, the updated credential metadata and corresponding new MAC
    // will be returned in |new_cred_metadata| and |new_mac|.
    //
    // In all cases, the resulting root hash is returned in |new_root|.
    virtual StatusOr<CredentialTreeResult> ResetCredential(
        const uint64_t label,
        const std::vector<std::vector<uint8_t>>& h_aux,
        const std::vector<uint8_t>& orig_cred_metadata,
        const brillo::SecureBlob& reset_secret) = 0;

    // Retrieves the replay log.
    //
    // The current on-disk root hash is supplied via |cur_disk_root_hash|.
    virtual StatusOr<GetLogResult> GetLog(
        const std::vector<uint8_t>& cur_disk_root_hash) = 0;

    // Replays the log operation referenced by |log_entry_root|, where
    // |log_entry_root| is the resulting root hash after the operation, and is
    // retrieved from the log entry.
    // |h_aux| and |orig_cred_metadata| refer to, respectively, the list of
    // auxiliary hashes and the original credential metadata associated with the
    // label concerned (available in the log entry). The resulting metadata and
    // MAC are stored in |new_cred_metadata| and |new_mac|.
    virtual StatusOr<ReplayLogOperationResult> ReplayLogOperation(
        const brillo::Blob& log_entry_root,
        const std::vector<brillo::Blob>& h_aux,
        const brillo::Blob& orig_cred_metadata) = 0;

    // Looks into the metadata and retrieves the number of wrong authentication
    // attempts.
    virtual StatusOr<int> GetWrongAuthAttempts(
        const brillo::Blob& cred_metadata) = 0;

    // Looks into the metadata and retrieves the delay schedule.
    virtual StatusOr<DelaySchedule> GetDelaySchedule(
        const brillo::Blob& cred_metadata) = 0;

    // Get the remaining delay in seconds.
    virtual StatusOr<uint32_t> GetDelayInSeconds(
        const brillo::Blob& cred_metadata) = 0;

   protected:
    PinWeaver() = default;
    ~PinWeaver() = default;
  };

  // Vendor provide the vendor specific commands.
  class Vendor {
   public:
    // Gets the family.
    virtual StatusOr<uint32_t> GetFamily() = 0;

    // Gets the spec level.
    virtual StatusOr<uint64_t> GetSpecLevel() = 0;

    // Gets the manufacturer.
    virtual StatusOr<uint32_t> GetManufacturer() = 0;

    // Gets the TPM model.
    virtual StatusOr<uint32_t> GetTpmModel() = 0;

    // Gets the TPM firmware version.
    virtual StatusOr<uint64_t> GetFirmwareVersion() = 0;

    // Gets the vendor specific string.
    virtual StatusOr<brillo::Blob> GetVendorSpecific() = 0;

    // Gets the TPM fingerprint.
    virtual StatusOr<int32_t> GetFingerprint() = 0;

    // Is the SRK ROCA vulnerable or not.
    virtual StatusOr<bool> IsSrkRocaVulnerable() = 0;

    // Gets the IFX upgrade information.
    virtual StatusOr<brillo::Blob> GetIFXFieldUpgradeInfo() = 0;

    // Declares the TPM firmware is stable.
    virtual Status DeclareTpmFirmwareStable() = 0;

    // Sends the raw |command|.
    virtual StatusOr<brillo::Blob> SendRawCommand(
        const brillo::Blob& command) = 0;

   protected:
    Vendor() = default;
    ~Vendor() = default;
  };

  virtual ~Backend() = default;

  // A helper to get the subclass pointer with subclass type.
  template <class SubClass>
  SubClass* Get() {
    if constexpr (std::is_same_v<SubClass, State>)
      return GetState();
    else if constexpr (std::is_same_v<SubClass, DAMitigation>)
      return GetDAMitigation();
    else if constexpr (std::is_same_v<SubClass, Storage>)
      return GetStorage();
    else if constexpr (std::is_same_v<SubClass, RoData>)
      return GetRoData();
    else if constexpr (std::is_same_v<SubClass, Sealing>)
      return GetSealing();
    else if constexpr (std::is_same_v<SubClass, SignatureSealing>)
      return GetSignatureSealing();
    else if constexpr (std::is_same_v<SubClass, Deriving>)
      return GetDeriving();
    else if constexpr (std::is_same_v<SubClass, Encryption>)
      return GetEncryption();
    else if constexpr (std::is_same_v<SubClass, Signing>)
      return GetSigning();
    else if constexpr (std::is_same_v<SubClass, KeyManagement>)
      return GetKeyManagement();
    else if constexpr (std::is_same_v<SubClass, SessionManagement>)
      return GetSessionManagement();
    else if constexpr (std::is_same_v<SubClass, Config>)
      return GetConfig();
    else if constexpr (std::is_same_v<SubClass, Random>)
      return GetRandom();
    else if constexpr (std::is_same_v<SubClass, PinWeaver>)
      return GetPinWeaver();
    else if constexpr (std::is_same_v<SubClass, Vendor>)
      return GetVendor();
    NOTREACHED() << "Should not reach here.";
  }

 protected:
  // A helper to give subclasses the ability to access the backend.
  template <typename BackendType>
  class SubClassHelper {
   public:
    explicit SubClassHelper(BackendType& backend) : backend_(backend) {}

   protected:
    ~SubClassHelper() = default;
    BackendType& backend_;
  };

 private:
  virtual State* GetState() = 0;
  virtual DAMitigation* GetDAMitigation() = 0;
  virtual Storage* GetStorage() = 0;
  virtual RoData* GetRoData() = 0;
  virtual Sealing* GetSealing() = 0;
  virtual SignatureSealing* GetSignatureSealing() = 0;
  virtual Deriving* GetDeriving() = 0;
  virtual Encryption* GetEncryption() = 0;
  virtual Signing* GetSigning() = 0;
  virtual KeyManagement* GetKeyManagement() = 0;
  virtual SessionManagement* GetSessionManagement() = 0;
  virtual Config* GetConfig() = 0;
  virtual Random* GetRandom() = 0;
  virtual PinWeaver* GetPinWeaver() = 0;
  virtual Vendor* GetVendor() = 0;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_BACKEND_H_
