// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STUB_TPM_H_
#define CRYPTOHOME_STUB_TPM_H_

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <map>
#include <set>
#include <string>

#include "cryptohome/tpm.h"

namespace cryptohome {

class StubTpm : public Tpm {
 public:
  using SecureBlob = brillo::SecureBlob;

  StubTpm() {}
  ~StubTpm() override {}

  // See tpm.h for comments
  TpmVersion GetVersion() override { return TpmVersion::TPM_UNKNOWN; }
  hwsec::error::TPMErrorBase EncryptBlob(TpmKeyHandle key_handle,
                                         const SecureBlob& plaintext,
                                         const SecureBlob& key,
                                         SecureBlob* ciphertext) override {
    return hwsec_foundation::error::CreateError<hwsec::error::TPMError>(
        "stub tpm operation", hwsec::error::TPMRetryAction::kNoRetry);
  }
  hwsec::error::TPMErrorBase DecryptBlob(
      TpmKeyHandle key_handle,
      const SecureBlob& ciphertext,
      const SecureBlob& key,
      const std::map<uint32_t, std::string>& pcr_map,
      SecureBlob* plaintext) override {
    return hwsec_foundation::error::CreateError<hwsec::error::TPMError>(
        "stub tpm operation", hwsec::error::TPMRetryAction::kNoRetry);
  }
  hwsec::error::TPMErrorBase GetAuthValue(
      base::Optional<TpmKeyHandle> key_handle,
      const brillo::SecureBlob& pass_blob,
      brillo::SecureBlob* auth_value) override {
    return hwsec_foundation::error::CreateError<hwsec::error::TPMError>(
        "stub tpm operation", hwsec::error::TPMRetryAction::kNoRetry);
  }
  hwsec::error::TPMErrorBase GetEccAuthValue(
      base::Optional<TpmKeyHandle> key_handle,
      const brillo::SecureBlob& pass_blob,
      brillo::SecureBlob* auth_value) override {
    return hwsec_foundation::error::CreateError<hwsec::error::TPMError>(
        "stub tpm operation", hwsec::error::TPMRetryAction::kNoRetry);
  }

  hwsec::error::TPMErrorBase SealToPcrWithAuthorization(
      const SecureBlob& plaintext,
      const SecureBlob& auth_value,
      const std::map<uint32_t, std::string>& pcr_map,
      SecureBlob* sealed_data) override {
    return hwsec_foundation::error::CreateError<hwsec::error::TPMError>(
        "stub tpm operation", hwsec::error::TPMRetryAction::kNoRetry);
  }
  hwsec::error::TPMErrorBase PreloadSealedData(
      const SecureBlob& sealed_data, ScopedKeyHandle* preload_handle) override {
    return hwsec_foundation::error::CreateError<hwsec::error::TPMError>(
        "stub tpm operation", hwsec::error::TPMRetryAction::kNoRetry);
  }
  hwsec::error::TPMErrorBase UnsealWithAuthorization(
      base::Optional<TpmKeyHandle> preload_handle,
      const SecureBlob& sealed_data,
      const SecureBlob& auth_value,
      const std::map<uint32_t, std::string>& pcr_map,
      SecureBlob* plaintext) override {
    return hwsec_foundation::error::CreateError<hwsec::error::TPMError>(
        "stub tpm operation", hwsec::error::TPMRetryAction::kNoRetry);
  }
  hwsec::error::TPMErrorBase GetPublicKeyHash(TpmKeyHandle key_handle,
                                              SecureBlob* hash) override {
    return nullptr;
  }
  bool IsEnabled() override { return false; }
  bool IsOwned() override { return false; }
  bool ReadNvram(uint32_t index, SecureBlob* blob) override { return false; }
  bool IsNvramDefined(uint32_t index) override { return false; }
  bool IsNvramLocked(uint32_t index) override { return false; }
  unsigned int GetNvramSize(uint32_t index) override { return 0; }
  hwsec::error::TPMErrorBase GetRandomDataBlob(size_t length,
                                               brillo::Blob* data) override {
    return hwsec_foundation::error::CreateError<hwsec::error::TPMError>(
        "stub tpm operation", hwsec::error::TPMRetryAction::kNoRetry);
  }
  hwsec::error::TPMErrorBase GetRandomDataSecureBlob(
      size_t length, brillo::SecureBlob* data) override {
    return hwsec_foundation::error::CreateError<hwsec::error::TPMError>(
        "stub tpm operation", hwsec::error::TPMRetryAction::kNoRetry);
  }
  hwsec::error::TPMErrorBase GetAlertsData(Tpm::AlertsData* alerts) override {
    return hwsec_foundation::error::CreateError<hwsec::error::TPMError>(
        "stub tpm operation", hwsec::error::TPMRetryAction::kNoRetry);
  }
  bool DefineNvram(uint32_t index, size_t length, uint32_t flags) override {
    return false;
  }
  bool DestroyNvram(uint32_t index) override { return false; }
  bool WriteNvram(uint32_t index, const SecureBlob& blob) override {
    return false;
  }
  bool WriteLockNvram(uint32_t index) override { return false; }
  bool SealToPCR0(const brillo::SecureBlob& value,
                  brillo::SecureBlob* sealed_value) override {
    return false;
  }
  bool Unseal(const brillo::SecureBlob& sealed_value,
              brillo::SecureBlob* value) override {
    return false;
  }
  bool CreateDelegate(const std::set<uint32_t>& bound_pcrs,
                      uint8_t delegate_family_label,
                      uint8_t delegate_label,
                      brillo::Blob* delegate_blob,
                      brillo::Blob* delegate_secret) override {
    return false;
  }
  bool Sign(const SecureBlob& key_blob,
            const SecureBlob& der_encoded_input,
            uint32_t bound_pcr_index,
            SecureBlob* signature) override {
    return false;
  }
  bool CreatePCRBoundKey(const std::map<uint32_t, std::string>& pcr_map,
                         AsymmetricKeyUsage key_type,
                         SecureBlob* key_blob,
                         SecureBlob* public_key_der,
                         SecureBlob* creation_blob) override {
    return false;
  }
  bool VerifyPCRBoundKey(const std::map<uint32_t, std::string>& pcr_map,
                         const SecureBlob& key_blob,
                         const SecureBlob& creation_blob) override {
    return false;
  }
  bool ExtendPCR(uint32_t pcr_index, const brillo::Blob& extension) override {
    return false;
  }
  bool ReadPCR(uint32_t pcr_index, brillo::Blob* pcr_value) override {
    return false;
  }
  bool IsEndorsementKeyAvailable() override { return false; }
  bool CreateEndorsementKey() override { return false; }
  bool TakeOwnership(int max_timeout_tries,
                     const SecureBlob& owner_password) override {
    return false;
  }
  bool WrapRsaKey(const SecureBlob& public_modulus,
                  const SecureBlob& prime_factor,
                  SecureBlob* wrapped_key) override {
    return false;
  }
  bool CreateWrappedEccKey(SecureBlob* wrapped_key) override { return false; }
  hwsec::error::TPMErrorBase LoadWrappedKey(
      const SecureBlob& wrapped_key, ScopedKeyHandle* key_handle) override {
    return hwsec_foundation::error::CreateError<hwsec::error::TPMError>(
        "stub tpm operation", hwsec::error::TPMRetryAction::kNoRetry);
  }
  bool LegacyLoadCryptohomeKey(ScopedKeyHandle* key_handle,
                               SecureBlob* key_blob) override {
    return false;
  }
  void CloseHandle(TpmKeyHandle key_handle) override{};
  void GetStatus(base::Optional<TpmKeyHandle> key,
                 TpmStatusInfo* status) override {}
  bool GetDictionaryAttackInfo(int* counter,
                               int* threshold,
                               bool* lockout,
                               int* seconds_remaining) override {
    return false;
  }
  bool ResetDictionaryAttackMitigation(
      const brillo::Blob& delegate_blob,
      const brillo::Blob& delegate_secret) override {
    return false;
  }
  void DeclareTpmFirmwareStable() override {}
  bool RemoveOwnerDependency(Tpm::TpmOwnerDependency dependency) override {
    return true;
  }
  bool ClearStoredPassword() override { return true; }
  bool GetVersionInfo(TpmVersionInfo* version_info) override { return false; }
  bool GetIFXFieldUpgradeInfo(IFXFieldUpgradeInfo* info) override {
    return false;
  }
  bool GetRsuDeviceId(std::string* device_id) override { return false; }
  LECredentialBackend* GetLECredentialBackend() override { return nullptr; }
  SignatureSealingBackend* GetSignatureSealingBackend() override {
    return nullptr;
  }
  cryptorecovery::RecoveryCryptoTpmBackend* GetRecoveryCryptoBackend()
      override {
    return nullptr;
  }
  hwsec::error::TPMErrorBase IsDelegateBoundToPcr(bool* result) override {
    *result = true;
    return nullptr;
  }
  bool DelegateCanResetDACounter() override { return true; }
  bool IsOwnerPasswordPresent() override { return false; }
  bool HasResetLockPermissions() override { return false; }
  bool OwnerWriteNvram(uint32_t index,
                       const brillo::SecureBlob& blob) override {
    return false;
  }
  hwsec::error::TPMErrorBase IsSrkRocaVulnerable(bool*) override {
    return hwsec_foundation::error::CreateError<hwsec::error::TPMError>(
        "stub tpm operation", hwsec::error::TPMRetryAction::kNoRetry);
  }
  bool GetDelegate(brillo::Blob* blob,
                   brillo::Blob* secret,
                   bool* has_reset_lock_permissions) override {
    return false;
  }
  std::map<uint32_t, std::string> GetPcrMap(
      const std::string& obfuscated_username,
      bool use_extended_pcr) const override {
    return {};
  }
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STUB_TPM_H_
