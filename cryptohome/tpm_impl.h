// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_TPM_IMPL_H_
#define CRYPTOHOME_TPM_IMPL_H_

#include <base/macros.h>

#include <libhwsec/error/tpm1_error.h>
#include <tpm_manager/client/tpm_manager_utility.h>
#include <trousers/scoped_tss_type.h>
#include <trousers/tss.h>
#include <trousers/trousers.h>  // NOLINT(build/include_alpha)

#include <stdint.h>

#include <map>
#include <memory>
#include <set>
#include <string>

#include "cryptohome/signature_sealing_backend_tpm1_impl.h"
#include "cryptohome/tpm.h"

namespace cryptohome {


class TpmImpl : public Tpm {
 public:
  TpmImpl();
  TpmImpl(const TpmImpl&) = delete;
  TpmImpl& operator=(const TpmImpl&) = delete;

  virtual ~TpmImpl();

  void SetTpmManagerUtilityForTesting(
      tpm_manager::TpmManagerUtility* tpm_manager_utility);

  // Tpm methods
  TpmVersion GetVersion() override { return TpmVersion::TPM_1_2; }
  TpmRetryAction EncryptBlob(TpmKeyHandle key_handle,
                             const brillo::SecureBlob& plaintext,
                             const brillo::SecureBlob& key,
                             brillo::SecureBlob* ciphertext) override;
  TpmRetryAction DecryptBlob(TpmKeyHandle key_handle,
                             const brillo::SecureBlob& ciphertext,
                             const brillo::SecureBlob& key,
                             const std::map<uint32_t, std::string>& pcr_map,
                             brillo::SecureBlob* plaintext) override;
  TpmRetryAction SealToPcrWithAuthorization(
      const brillo::SecureBlob& plaintext,
      const brillo::SecureBlob& auth_value,
      const std::map<uint32_t, std::string>& pcr_map,
      brillo::SecureBlob* sealed_data) override;
  TpmRetryAction PreloadSealedData(const brillo::SecureBlob& sealed_data,
                                   ScopedKeyHandle* preload_handle) override;
  TpmRetryAction UnsealWithAuthorization(
      base::Optional<TpmKeyHandle> preload_handle,
      const brillo::SecureBlob& sealed_data,
      const brillo::SecureBlob& auth_value,
      const std::map<uint32_t, std::string>& pcr_map,
      brillo::SecureBlob* plaintext) override;
  TpmRetryAction GetPublicKeyHash(TpmKeyHandle key_handle,
                                  brillo::SecureBlob* hash) override;
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
  TpmRetryAction LoadWrappedKey(const brillo::SecureBlob& wrapped_key,
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
  bool ResetDictionaryAttackMitigation(
      const brillo::Blob& delegate_blob,
      const brillo::Blob& delegate_secret) override;
  void DeclareTpmFirmwareStable() override {}
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
  base::Optional<bool> IsDelegateBoundToPcr() override;
  bool DelegateCanResetDACounter() override;
  // Returns the map with expected PCR values for the user.
  std::map<uint32_t, std::string> GetPcrMap(
      const std::string& obfuscated_username,
      bool use_extended_pcr) const override;

  bool CreatePolicyWithRandomPassword(TSS_HCONTEXT context_handle,
                                      TSS_FLAG policy_type,
                                      TSS_HPOLICY* policy_handle);

  // Gets a handle to the SRK.
  bool LoadSrk(TSS_HCONTEXT context_handle,
               TSS_HKEY* srk_handle,
               TSS_RESULT* result);

  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object iff the owner password is available and
  // authorization is successfully acquired.
  bool ConnectContextAsOwner(TSS_HCONTEXT* context_handle,
                             TSS_HTPM* tpm_handle);

  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object authorized by the given delegation.
  bool ConnectContextAsDelegate(const brillo::Blob& delegate_blob,
                                const brillo::Blob& delegate_secret,
                                TSS_HCONTEXT* context,
                                TSS_HTPM* tpm_handle);

  // Wrapper for Tspi_GetAttribData.
  TpmRetryAction GetDataAttribute(TSS_HCONTEXT context,
                                  TSS_HOBJECT object,
                                  TSS_FLAG flag,
                                  TSS_FLAG sub_flag,
                                  brillo::SecureBlob* data) const;

  // Creates Trousers key object for the RSA public key, given its public
  // modulus in |key_modulus|, creation flags in |key_flags|, signature scheme
  // or |TSS_SS_NONE| in |signature_scheme|, encryption scheme or |TSS_ES_NONE|
  // in |encryption_scheme|. The key's public exponent is assumed to be 65537.
  // Populates |key_handle| with the loaded key handle.
  bool CreateRsaPublicKeyObject(TSS_HCONTEXT tpm_context,
                                const brillo::Blob& key_modulus,
                                TSS_FLAG key_flags,
                                UINT32 signature_scheme,
                                UINT32 encryption_scheme,
                                TSS_HKEY* key_handle);

  // Copies the |pass_blob| to |auth_value|.
  // The input |pass_blob| must have 256 bytes.
  bool GetAuthValue(base::Optional<TpmKeyHandle> key_handle,
                    const brillo::SecureBlob& pass_blob,
                    brillo::SecureBlob* auth_value) override;

 private:
  // Processes the delegate blob and establishes if it's bound to any PCR. Also
  // keeps the information about reset_lock_permissions. Returns |true| iff the
  // attributes of the delegate is successfully determined.
  //
  // TODO(b/169392230): Remove this function once tpm manager performs the
  // check.
  bool SetDelegateData(const brillo::Blob& delegate_blob,
                       bool has_reset_lock_permissions);

  // Connects to the TPM and return its context at |context_handle|.
  hwsec::error::TPM1Error OpenAndConnectTpm(TSS_HCONTEXT* context_handle);

  // Gets the Public Key blob associated with |key_handle|.
  hwsec::error::TPM1Error GetPublicKeyBlob(TSS_HCONTEXT context_handle,
                                           TSS_HKEY key_handle,
                                           brillo::SecureBlob* data_out) const;

  // Gets the key blob associated with |key_handle|.
  bool GetKeyBlob(TSS_HCONTEXT context_handle,
                  TSS_HKEY key_handle,
                  brillo::SecureBlob* data_out,
                  TSS_RESULT* result) const;

  // Tries to connect to the TPM
  TSS_HCONTEXT ConnectContext();

  // Populates |context_handle| with a valid TSS_HCONTEXT and |tpm_handle| with
  // its matching TPM object iff the context can be created and a TPM object
  // exists in the TSS.
  bool ConnectContextAsUser(TSS_HCONTEXT* context_handle, TSS_HTPM* tpm_handle);

  // Gets a handle to the TPM from the specified context
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   tpm_handle (OUT) - The handle for the TPM on success
  bool GetTpm(TSS_HCONTEXT context_handle, TSS_HTPM* tpm_handle);

  // Gets a handle to the TPM from the specified context with the given owner
  // password
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   owner_password - The owner password to use when getting the handle
  //   tpm_handle (OUT) - The handle for the TPM on success
  bool GetTpmWithAuth(TSS_HCONTEXT context_handle,
                      const brillo::SecureBlob& owner_password,
                      TSS_HTPM* tpm_handle);

  // Gets a handle to the TPM from the specified context with the given
  // delegation.
  //
  // Parameters
  //   context_handle - The context handle for the TPM session
  //   delegate_blob - The delegate blob to use when getting the handle
  //   delegate_secret - The delegate secret to use when getting the handle
  //   tpm_handle (OUT) - The handle for the TPM on success
  bool GetTpmWithDelegation(TSS_HCONTEXT context_handle,
                            const brillo::Blob& delegate_blob,
                            const brillo::Blob& delegate_secret,
                            TSS_HTPM* tpm_handle);

  // Assigns the authorization value to object.
  bool SetAuthValue(TSS_HCONTEXT context_handle,
                    trousers::ScopedTssKey* enc_handle,
                    TSS_HTPM tpm_handle,
                    const brillo::SecureBlob& auth_value);

  // Initializes |tpm_manager_utility_|; returns |true| iff successful.
  bool InitializeTpmManagerUtility();

  // Calls |TpmManagerUtility::GetTpmStatus| and stores the result into
  // |is_enabled_|, |is_owned_|, and |last_tpm_manager_data_| for later use.
  bool CacheTpmManagerStatus();

  // Attempts to get |tpm_manager::LocalData| from signal or by explicitly
  // querying it. Returns |true| iff either approach succeeds. Behind the scene,
  // the function attempts to update the local data when it's available from
  // ownership taken signal. Otherwise, for any reason why we don't have it from
  // ownership taken signal, it actively query tpm status by a dbus request.
  // This intuitive way can be seen an  overkill sometimes(e.g. The signal is
  // waiting to be connected); however this conservative approach can avoid the
  // data loss due to some potential issues (e.g. unexpectedly long waiting time
  // until the signal is confirmed to be connected).
  bool UpdateLocalDataFromTpmManager();

  // Gets delegate from tpm manager and call feed the value to |SetDelegate|.
  bool SetDelegateDataFromTpmManager();

  // Member variables
  brillo::SecureBlob srk_auth_;

  // If TPM ownership is taken, owner_password_ contains the password used
  brillo::SecureBlob owner_password_;

  // Indicates if the TPM is enabled
  bool is_enabled_{false};

  // Indicates if the TPM is owned
  bool is_owned_{false};

  // Indicates if the delegate is bound to PCR.
  base::Optional<bool> is_delegate_bound_to_pcr_;

  // Indicates if the delegate is allowed to reset dictional attack counter.
  bool has_reset_lock_permissions_ = false;

  // Tpm Context information
  trousers::ScopedTssContext tpm_context_;

  // A single instance of the backend for signature-sealing operations that is
  // returned from GetSignatureSealingBackend().
  SignatureSealingBackendTpm1Impl signature_sealing_backend_{this};

  // wrapped tpm_manager proxy to get information from |tpm_manager|.
  tpm_manager::TpmManagerUtility* tpm_manager_utility_ = nullptr;

  // Indicates if the delegate has been set and the parent class |TpmImpl|
  // already has the information about the owner delegate we have from tpm
  // manager.
  bool has_set_delegate_data_ = false;

  // This flag indicates |CacheTpmManagerStatus| shall be called when the
  // ownership taken signal is confirmed to be connected.
  bool shall_cache_tpm_manager_status_{true};

  // Records |LocalData| from tpm_manager last time we query, either by
  // explicitly requesting the update or from dbus signal.
  tpm_manager::LocalData last_tpm_manager_data_;

  // Cache of TPM version info, base::nullopt if cache doesn't exist.
  base::Optional<TpmVersionInfo> version_info_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_IMPL_H_
