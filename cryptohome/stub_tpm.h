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
  TpmRetryAction EncryptBlob(TpmKeyHandle key_handle,
                             const SecureBlob& plaintext,
                             const SecureBlob& key,
                             SecureBlob* ciphertext) override {
    return kTpmRetryFatal;
  }
  TpmRetryAction DecryptBlob(TpmKeyHandle key_handle,
                             const SecureBlob& ciphertext,
                             const SecureBlob& key,
                             const std::map<uint32_t, std::string>& pcr_map,
                             SecureBlob* plaintext) override {
    return kTpmRetryFatal;
  }
  TpmRetryAction SealToPcrWithAuthorization(
      TpmKeyHandle key_handle,
      const SecureBlob& plaintext,
      const SecureBlob& auth_blob,
      const std::map<uint32_t, std::string>& pcr_map,
      SecureBlob* sealed_data) override {
    return kTpmRetryFatal;
  }
  TpmRetryAction PreloadSealedData(const SecureBlob& sealed_data,
                                   ScopedKeyHandle* preload_handle) override {
    return kTpmRetryFatal;
  }
  TpmRetryAction UnsealWithAuthorization(
      TpmKeyHandle key_handle,
      base::Optional<TpmKeyHandle> preload_handle,
      const SecureBlob& sealed_data,
      const SecureBlob& auth_blob,
      const std::map<uint32_t, std::string>& pcr_map,
      SecureBlob* plaintext) override {
    return kTpmRetryFatal;
  }
  TpmRetryAction GetPublicKeyHash(TpmKeyHandle key_handle,
                                  SecureBlob* hash) override {
    return kTpmRetryNone;
  }
  bool IsEnabled() override { return false; }
  bool IsOwned() override { return false; }
  bool ReadNvram(uint32_t index, SecureBlob* blob) override { return false; }
  bool IsNvramDefined(uint32_t index) override { return false; }
  bool IsNvramLocked(uint32_t index) override { return false; }
  unsigned int GetNvramSize(uint32_t index) override { return 0; }
  bool GetOwnerPassword(brillo::SecureBlob* owner_password) override {
    return false;
  }
  bool PerformEnabledOwnedCheck(bool* enabled, bool* owned) override {
    return false;
  }
  bool GetRandomDataBlob(size_t length, brillo::Blob* data) override {
    return false;
  }
  bool GetRandomDataSecureBlob(size_t length,
                               brillo::SecureBlob* data) override {
    return false;
  }
  bool GetAlertsData(Tpm::AlertsData* alerts) override { return false; }
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
  TpmRetryAction LoadWrappedKey(const SecureBlob& wrapped_key,
                                ScopedKeyHandle* key_handle) override {
    return kTpmRetryFatal;
  }
  bool LegacyLoadCryptohomeKey(ScopedKeyHandle* key_handle,
                               SecureBlob* key_blob) override {
    return false;
  }
  void CloseHandle(TpmKeyHandle key_handle) override{};
  void GetStatus(TpmKeyHandle key, TpmStatusInfo* status) override {}
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
  bool GetRsuDeviceId(std::string* device_id) { return false; }
  LECredentialBackend* GetLECredentialBackend() override { return nullptr; }
  SignatureSealingBackend* GetSignatureSealingBackend() override {
    return nullptr;
  }
  void SetDelegateData(const std::string& delegate_blob,
                       bool has_reset_lock_permissions) override {}
  base::Optional<bool> IsDelegateBoundToPcr() override { return true; }
  bool DelegateCanResetDACounter() override { return true; }
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STUB_TPM_H_
