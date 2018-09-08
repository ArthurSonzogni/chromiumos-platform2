// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_TPM2_IMPL_H_
#define CRYPTOHOME_TPM2_IMPL_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>

#include <base/macros.h>
#include <base/threading/platform_thread.h>
#include <base/threading/thread.h>
#include <tpm_manager/client/tpm_nvram_dbus_proxy.h>
#include <tpm_manager/client/tpm_ownership_dbus_proxy.h>
#include <tpm_manager/common/tpm_manager.pb.h>
#include <tpm_manager/common/tpm_nvram_interface.h>
#include <tpm_manager/common/tpm_ownership_interface.h>
#include <trunks/hmac_session.h>
#include <trunks/tpm_generated.h>
#include <trunks/tpm_state.h>
#include <trunks/tpm_utility.h>
#include <trunks/trunks_factory.h>
#include <trunks/trunks_factory_impl.h>

#include "cryptohome/le_credential_backend.h"
#include "cryptohome/pinweaver_le_credential_backend.h"
#include "cryptohome/signature_sealing_backend_tpm2_impl.h"
#include "cryptohome/tpm.h"

namespace trunks {

class AuthorizationDelegate;

}  // namespace trunks

namespace cryptohome {

const uint32_t kDefaultTpmRsaModulusSize = 2048;
const uint32_t kDefaultTpmPublicExponent = 0x10001;
const uint32_t kLockboxIndex = 0x800004;
const uint32_t kLockboxPCR = 15;

class Tpm2Impl : public Tpm {
 public:
  // This object may be used across multiple threads but the Trunks D-Bus proxy
  // can only be used on a single thread. This structure holds all Trunks client
  // objects for a particular thread.
  struct TrunksClientContext {
    trunks::TrunksFactory* factory;
    std::unique_ptr<trunks::TrunksFactoryImpl> factory_impl;
    std::unique_ptr<trunks::TpmState> tpm_state;
    std::unique_ptr<trunks::TpmUtility> tpm_utility;
  };

  Tpm2Impl() = default;
  // Does not take ownership of pointers.
  Tpm2Impl(trunks::TrunksFactory* factory,
           tpm_manager::TpmOwnershipInterface* tpm_owner,
           tpm_manager::TpmNvramInterface* tpm_nvram);
  virtual ~Tpm2Impl() = default;

  // Tpm methods
  TpmVersion GetVersion() override { return TpmVersion::TPM_2_0; }
  TpmRetryAction EncryptBlob(TpmKeyHandle key_handle,
                             const brillo::SecureBlob& plaintext,
                             const brillo::SecureBlob& key,
                             brillo::SecureBlob* ciphertext) override;
  TpmRetryAction DecryptBlob(TpmKeyHandle key_handle,
                             const brillo::SecureBlob& ciphertext,
                             const brillo::SecureBlob& key,
                             const std::map<uint32_t, std::string>& pcr_map,
                             brillo::SecureBlob* plaintext) override;
  TpmRetryAction GetPublicKeyHash(TpmKeyHandle key_handle,
                                  brillo::SecureBlob* hash) override;
  bool GetOwnerPassword(brillo::Blob* owner_password) override;
  bool IsEnabled() override;
  void SetIsEnabled(bool enabled) override;
  bool IsOwned() override;
  void SetIsOwned(bool owned) override;
  bool PerformEnabledOwnedCheck(bool* enabled, bool* owned) override;
  bool IsInitialized() override;
  void SetIsInitialized(bool done) override;
  bool IsBeingOwned() override;
  void SetIsBeingOwned(bool value) override;
  bool GetRandomDataBlob(size_t length, brillo::Blob* data) override;
  bool GetRandomDataSecureBlob(size_t length,
                               brillo::SecureBlob* data) override;
  bool GetAlertsData(Tpm::AlertsData* alerts) override;
  bool DefineNvram(uint32_t index, size_t length, uint32_t flags) override;
  bool DestroyNvram(uint32_t index) override;
  bool WriteNvram(uint32_t index, const brillo::SecureBlob& blob) override;
  bool ReadNvram(uint32_t index, brillo::SecureBlob* blob) override;
  bool IsNvramDefined(uint32_t index) override;
  bool IsNvramLocked(uint32_t index) override;
  bool WriteLockNvram(uint32_t index) override;
  unsigned int GetNvramSize(uint32_t index) override;
  TpmRetryAction GetEndorsementPublicKey(
      brillo::SecureBlob* ek_public_key) override;
  TpmRetryAction GetEndorsementPublicKeyWithDelegate(
      brillo::SecureBlob* ek_public_key,
      const brillo::SecureBlob& delegate_blob,
      const brillo::SecureBlob& delegate_secret) override;
  bool GetEndorsementCredential(brillo::SecureBlob* credential) override;
  bool MakeIdentity(brillo::SecureBlob* identity_public_key_der,
                    brillo::SecureBlob* identity_public_key,
                    brillo::SecureBlob* identity_key_blob,
                    brillo::SecureBlob* identity_binding,
                    brillo::SecureBlob* identity_label,
                    brillo::SecureBlob* pca_public_key,
                    brillo::SecureBlob* endorsement_credential,
                    brillo::SecureBlob* platform_credential,
                    brillo::SecureBlob* conformance_credential) override;
  bool QuotePCR(uint32_t pcr_index,
                const brillo::SecureBlob& identity_key_blob,
                const brillo::SecureBlob& external_data,
                brillo::Blob* pcr_value,
                brillo::SecureBlob* quoted_data,
                brillo::SecureBlob* quote) override;
  bool SealToPCR0(const brillo::Blob& value,
                  brillo::Blob* sealed_value) override;
  bool Unseal(const brillo::Blob& sealed_value, brillo::Blob* value) override;
  bool CreateCertifiedKey(const brillo::SecureBlob& identity_key_blob,
                          const brillo::SecureBlob& external_data,
                          brillo::SecureBlob* certified_public_key,
                          brillo::SecureBlob* certified_public_key_der,
                          brillo::SecureBlob* certified_key_blob,
                          brillo::SecureBlob* certified_key_info,
                          brillo::SecureBlob* certified_key_proof) override;
  bool CreateDelegate(const std::set<uint32_t>& bound_pcrs,
                      uint8_t delegate_family_label,
                      uint8_t delegate_label,
                      brillo::SecureBlob* delegate_blob,
                      brillo::SecureBlob* delegate_secret) override;
  bool ActivateIdentity(const brillo::SecureBlob& delegate_blob,
                        const brillo::SecureBlob& delegate_secret,
                        const brillo::SecureBlob& identity_key_blob,
                        const brillo::SecureBlob& encrypted_asym_ca,
                        const brillo::SecureBlob& encrypted_sym_ca,
                        brillo::SecureBlob* identity_credential) override;
  bool Sign(const brillo::SecureBlob& key_blob,
            const brillo::SecureBlob& input,
            uint32_t bound_pcr_index,
            brillo::SecureBlob* signature) override;
  bool CreatePCRBoundKey(const std::map<uint32_t, std::string>& pcr_map,
                         AsymmetricKeyUsage key_type,
                         brillo::SecureBlob* key_blob,
                         brillo::SecureBlob* public_key_der,
                         brillo::SecureBlob* creation_blob) override;
  bool VerifyPCRBoundKey(const std::map<uint32_t, std::string>& pcr_map,
                         const brillo::SecureBlob& key_blob,
                         const brillo::SecureBlob& creation_blob) override;
  bool ExtendPCR(uint32_t pcr_index, const brillo::Blob& extension) override;
  bool ReadPCR(uint32_t pcr_index, brillo::Blob* pcr_value) override;
  bool IsEndorsementKeyAvailable() override;
  bool CreateEndorsementKey() override;
  bool TakeOwnership(int max_timeout_tries,
                     const brillo::SecureBlob& owner_password) override;
  bool InitializeSrk(const brillo::SecureBlob& owner_password) override;
  bool ChangeOwnerPassword(const brillo::SecureBlob& previous_owner_password,
                           const brillo::SecureBlob& owner_password) override;
  bool TestTpmAuth(const brillo::SecureBlob& owner_password) override;
  void SetOwnerPassword(const brillo::SecureBlob& owner_password) override;
  bool WrapRsaKey(const brillo::SecureBlob& public_modulus,
                  const brillo::SecureBlob& prime_factor,
                  brillo::SecureBlob* wrapped_key) override;
  TpmRetryAction LoadWrappedKey(const brillo::SecureBlob& wrapped_key,
                                ScopedKeyHandle* key_handle) override;
  bool LegacyLoadCryptohomeKey(ScopedKeyHandle* key_handle,
                               brillo::SecureBlob* key_blob) override;
  void CloseHandle(TpmKeyHandle key_handle) override;
  void GetStatus(TpmKeyHandle key, TpmStatusInfo* status) override;
  bool GetDictionaryAttackInfo(int* counter,
                               int* threshold,
                               bool* lockout,
                               int* seconds_remaining) override;
  bool ResetDictionaryAttackMitigation(
      const brillo::SecureBlob& delegate_blob,
      const brillo::SecureBlob& delegate_secret) override;
  void DeclareTpmFirmwareStable() override;
  bool RemoveOwnerDependency(
      TpmPersistentState::TpmOwnerDependency dependency) override;
  bool ClearStoredPassword() override;
  bool GetVersionInfo(TpmVersionInfo* version_info) override;
  bool GetIFXFieldUpgradeInfo(IFXFieldUpgradeInfo* info) override;
  bool SetUserType(Tpm::UserType type) override;
  LECredentialBackend* GetLECredentialBackend() override;
  SignatureSealingBackend* GetSignatureSealingBackend() override;

  // Gets the trunks objects for the current thread, initializing new ones if
  // necessary. Returns true on success.
  bool GetTrunksContext(TrunksClientContext** trunks);

  // Loads the key from its DER-encoded Subject Public Key Info. Key is of type
  // |key_type|. Algorithm scheme and hashing algorithm are passed via |scheme|
  // and |hash_alg|. Currently, only the RSA keys are supported.
  // The loaded key handle is returned via |key_handle|.
  bool LoadPublicKeyFromSpki(const brillo::Blob& public_key_spki_der,
                             AsymmetricKeyUsage key_type,
                             trunks::TPM_ALG_ID scheme,
                             trunks::TPM_ALG_ID hash_alg,
                             trunks::AuthorizationDelegate* session_delegate,
                             ScopedKeyHandle* key_handle);

 private:
  // This method given a Tpm generated public area, returns the DER encoded
  // public key.
  bool PublicAreaToPublicKeyDER(const trunks::TPMT_PUBLIC& public_area,
                                brillo::SecureBlob* public_key_der);

  // Starts tpm_manager_thread_ and initiates initialization of
  // default_tpm_owner_ and default_tpm_nvram_ if necessary. Returns true if the
  // thread was started and both tpm_owner_ and tpm_nvram_ are valid.
  bool InitializeTpmManagerClients();
  void InitializeClientsOnTpmManagerThread(base::WaitableEvent* completion);

  // Sends a request to the TPM Manager daemon that expects a ReplyProtoType and
  // waits for a reply. The |method| will be called on the |tpm_manager_thread_|
  // and must take a callback argument as defined in TpmOwnershipInterface or
  // TpmNvramInterface. InitializeTpmManagerClients() must be called
  // successfully before calling this method.
  template <typename ReplyProtoType, typename MethodType>
  void SendTpmManagerRequestAndWait(const MethodType& method,
                                    ReplyProtoType* reply_proto);

  // Updates tpm_status_ according to the requested |refresh_type|. Returns
  // true on success. Use |REFRESH_IF_NEEDED| for most calls. Use
  // |FORCE_REFRESH| for calls which are querying a field that can change at any
  // time, like the dictionary attack counter.
  enum class RefreshType { REFRESH_IF_NEEDED, FORCE_REFRESH };
  bool UpdateTpmStatus(RefreshType refresh_type);

  // Per-thread trunks object management.
  std::map<base::PlatformThreadId, std::unique_ptr<TrunksClientContext>>
      trunks_contexts_;
  TrunksClientContext external_trunks_context_;
  bool has_external_trunks_context_ = false;

  // The most recent status from tpm_managerd.
  tpm_manager::GetTpmStatusReply tpm_status_;

  // True, if the tpm firmware has been already successfully declared stable.
  bool fw_declared_stable_ = false;

  // Indicates if the TPM is being owned
  bool is_being_owned_ = false;

  // Specifies the currently set user type.
  Tpm::UserType cur_user_type_ = Tpm::UserType::Unknown;

  // A message loop thread dedicated for asynchronous communication with
  // tpm_managerd.
  base::Thread tpm_manager_thread_{"tpm_manager_thread"};
  tpm_manager::TpmOwnershipInterface* tpm_owner_ = nullptr;
  std::unique_ptr<tpm_manager::TpmOwnershipDBusProxy> default_tpm_owner_;
  tpm_manager::TpmNvramInterface* tpm_nvram_ = nullptr;
  std::unique_ptr<tpm_manager::TpmNvramDBusProxy> default_tpm_nvram_;
#if USE_PINWEAVER
  PinweaverLECredentialBackend le_credential_backend_{this};
#endif
  SignatureSealingBackendTpm2Impl signature_sealing_backend_{this};

  DISALLOW_COPY_AND_ASSIGN(Tpm2Impl);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM2_IMPL_H_
