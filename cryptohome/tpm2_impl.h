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
#include <tpm_manager/client/tpm_manager_utility.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <trunks/error_codes.h>
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
           tpm_manager::TpmManagerUtility* tpm_manager_utility);
  Tpm2Impl(const Tpm2Impl&) = delete;
  Tpm2Impl& operator=(const Tpm2Impl&) = delete;

  virtual ~Tpm2Impl() = default;

  // Tpm methods
  TpmVersion GetVersion() override { return TpmVersion::TPM_2_0; }
  hwsec::error::TPMErrorBase EncryptBlob(
      TpmKeyHandle key_handle,
      const brillo::SecureBlob& plaintext,
      const brillo::SecureBlob& key,
      brillo::SecureBlob* ciphertext) override;
  hwsec::error::TPMErrorBase DecryptBlob(
      TpmKeyHandle key_handle,
      const brillo::SecureBlob& ciphertext,
      const brillo::SecureBlob& key,
      const std::map<uint32_t, std::string>& pcr_map,
      brillo::SecureBlob* plaintext) override;
  hwsec::error::TPMErrorBase SealToPcrWithAuthorization(
      const brillo::SecureBlob& plaintext,
      const brillo::SecureBlob& auth_value,
      const std::map<uint32_t, std::string>& pcr_map,
      brillo::SecureBlob* sealed_data) override;
  hwsec::error::TPMErrorBase PreloadSealedData(
      const brillo::SecureBlob& sealed_data,
      ScopedKeyHandle* preload_handle) override;
  hwsec::error::TPMErrorBase UnsealWithAuthorization(
      base::Optional<TpmKeyHandle> preload_handle,
      const brillo::SecureBlob& sealed_data,
      const brillo::SecureBlob& auth_value,
      const std::map<uint32_t, std::string>& pcr_map,
      brillo::SecureBlob* plaintext) override;
  hwsec::error::TPMErrorBase GetPublicKeyHash(
      TpmKeyHandle key_handle, brillo::SecureBlob* hash) override;
  bool GetOwnerPassword(brillo::SecureBlob* owner_password) override;
  bool IsEnabled() override;
  bool IsOwned() override;
  bool IsOwnerPasswordPresent() override;
  bool HasResetLockPermissions() override;
  bool PerformEnabledOwnedCheck(bool* enabled, bool* owned) override;
  bool GetRandomDataBlob(size_t length, brillo::Blob* data) override;
  bool GetRandomDataSecureBlob(size_t length,
                               brillo::SecureBlob* data) override;
  bool GetAlertsData(Tpm::AlertsData* alerts) override;
  bool DefineNvram(uint32_t index, size_t length, uint32_t flags) override;
  bool DestroyNvram(uint32_t index) override;
  bool WriteNvram(uint32_t index, const brillo::SecureBlob& blob) override;
  bool OwnerWriteNvram(uint32_t index, const brillo::SecureBlob& blob) override;
  bool ReadNvram(uint32_t index, brillo::SecureBlob* blob) override;
  bool IsNvramDefined(uint32_t index) override;
  bool IsNvramLocked(uint32_t index) override;
  bool WriteLockNvram(uint32_t index) override;
  unsigned int GetNvramSize(uint32_t index) override;
  bool SealToPCR0(const brillo::SecureBlob& value,
                  brillo::SecureBlob* sealed_value) override;
  bool Unseal(const brillo::SecureBlob& sealed_value,
              brillo::SecureBlob* value) override;
  bool CreateDelegate(const std::set<uint32_t>& bound_pcrs,
                      uint8_t delegate_family_label,
                      uint8_t delegate_label,
                      brillo::Blob* delegate_blob,
                      brillo::Blob* delegate_secret) override;
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
  bool WrapRsaKey(const brillo::SecureBlob& public_modulus,
                  const brillo::SecureBlob& prime_factor,
                  brillo::SecureBlob* wrapped_key) override;
  hwsec::error::TPMErrorBase LoadWrappedKey(
      const brillo::SecureBlob& wrapped_key,
      ScopedKeyHandle* key_handle) override;
  bool LegacyLoadCryptohomeKey(ScopedKeyHandle* key_handle,
                               brillo::SecureBlob* key_blob) override;
  void CloseHandle(TpmKeyHandle key_handle) override;
  void GetStatus(base::Optional<TpmKeyHandle> key,
                 TpmStatusInfo* status) override;
  base::Optional<bool> IsSrkRocaVulnerable() override;
  bool GetDictionaryAttackInfo(int* counter,
                               int* threshold,
                               bool* lockout,
                               int* seconds_remaining) override;
  // Asynchronously resets DA lock in tpm_managerd.
  bool ResetDictionaryAttackMitigation(
      const brillo::Blob& /* delegate_blob */,
      const brillo::Blob& /* delegate_secret */) override;
  void DeclareTpmFirmwareStable() override;
  bool RemoveOwnerDependency(Tpm::TpmOwnerDependency dependency) override;
  bool ClearStoredPassword() override;
  bool GetVersionInfo(TpmVersionInfo* version_info) override;
  bool GetIFXFieldUpgradeInfo(IFXFieldUpgradeInfo* info) override;
  bool GetRsuDeviceId(std::string* device_id) override;
  LECredentialBackend* GetLECredentialBackend() override;
  SignatureSealingBackend* GetSignatureSealingBackend() override;
  bool GetDelegate(brillo::Blob* blob,
                   brillo::Blob* secret,
                   bool* has_reset_lock_permissions) override;

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

  base::Optional<bool> IsDelegateBoundToPcr() override;
  bool DelegateCanResetDACounter() override;
  std::map<uint32_t, std::string> GetPcrMap(
      const std::string& obfuscated_username,
      bool use_extended_pcr) const override;

  // Derives the |auth_value| by decrypting the |pass_blob| using |key_handle|
  // and hashing the result. The input |pass_blob| must have 256 bytes, the
  // size of cryptohome key modulus.
  hwsec::error::TPMErrorBase GetAuthValue(
      base::Optional<TpmKeyHandle> key_handle,
      const brillo::SecureBlob& pass_blob,
      brillo::SecureBlob* auth_value) override;

 private:
  // Initializes |tpm_manager_utility_|; returns |true| iff successful.
  bool InitializeTpmManagerUtility();

  // Calls |TpmManagerUtility::GetTpmStatus| and stores the result into
  // |is_enabled_|, |is_owned_|, and |last_tpm_manager_data_| for later use.
  bool CacheTpmManagerStatus();

  // This method given a Tpm generated public area, returns the DER encoded
  // public key.
  bool PublicAreaToPublicKeyDER(const trunks::TPMT_PUBLIC& public_area,
                                brillo::SecureBlob* public_key_der);

  // Updates tpm_status_ according to the requested |refresh_type|. Returns
  // true on success. Use |REFRESH_IF_NEEDED| for most calls. Use
  // |FORCE_REFRESH| for calls which are querying a field that can change at any
  // time, like the dictionary attack counter.
  enum class RefreshType { REFRESH_IF_NEEDED, FORCE_REFRESH };
  bool UpdateTpmStatus(RefreshType refresh_type);

  //  wrapped tpm_manager proxy to get information from |tpm_manager|.
  tpm_manager::TpmManagerUtility* tpm_manager_utility_{nullptr};

  // Per-thread trunks object management.
  std::map<base::PlatformThreadId, std::unique_ptr<TrunksClientContext>>
      trunks_contexts_;

  // Used to provide thread-safe access to trunks_contexts_.
  base::Lock trunks_contexts_lock_;

  TrunksClientContext external_trunks_context_;
  bool has_external_trunks_context_ = false;

  // Cache of TPM version info, base::nullopt if cache doesn't exist.
  base::Optional<TpmVersionInfo> version_info_;

  // True, if the tpm firmware has been already successfully declared stable.
  bool fw_declared_stable_ = false;

  // Indicates if the TPM is already enabled.
  bool is_enabled_ = false;

  // Indicates if the TPM is already owned.
  bool is_owned_ = false;

  // This flag indicates |CacheTpmManagerStatus| shall be called when the
  // ownership taken signal is confirmed to be connected.
  bool shall_cache_tpm_manager_status_ = true;

  // Records |LocalData| from tpm_manager last time we query, either by
  // explicitly requesting the update or from dbus signal.
  tpm_manager::LocalData last_tpm_manager_data_;

#if USE_PINWEAVER
  PinweaverLECredentialBackend le_credential_backend_{this};
#endif
  SignatureSealingBackendTpm2Impl signature_sealing_backend_{this};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM2_IMPL_H_
