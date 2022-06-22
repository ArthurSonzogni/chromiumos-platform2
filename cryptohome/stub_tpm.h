// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STUB_TPM_H_
#define CRYPTOHOME_STUB_TPM_H_

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include <libhwsec/factory/factory_impl.h>
#include <libhwsec/status.h>

#include "cryptohome/tpm.h"

namespace cryptohome {

class StubTpm : public Tpm {
 public:
  using SecureBlob = brillo::SecureBlob;

  StubTpm()
      : hwsec_factory_(std::make_unique<hwsec::FactoryImpl>()),
        hwsec_(hwsec_factory_->GetCryptohomeFrontend()),
        pinweaver_(hwsec_factory_->GetPinWeaverFrontend()) {}
  ~StubTpm() override {}

  // See tpm.h for comments
  TpmVersion GetVersion() override { return TpmVersion::TPM_UNKNOWN; }
  hwsec::Status EncryptBlob(TpmKeyHandle key_handle,
                            const SecureBlob& plaintext,
                            const SecureBlob& key,
                            SecureBlob* ciphertext) override {
    return hwsec_foundation::error::CreateError<hwsec::TPMError>(
        "stub tpm operation", hwsec::TPMRetryAction::kNoRetry);
  }
  hwsec::Status DecryptBlob(TpmKeyHandle key_handle,
                            const SecureBlob& ciphertext,
                            const SecureBlob& key,
                            SecureBlob* plaintext) override {
    return hwsec_foundation::error::CreateError<hwsec::TPMError>(
        "stub tpm operation", hwsec::TPMRetryAction::kNoRetry);
  }
  hwsec::Status GetAuthValue(std::optional<TpmKeyHandle> key_handle,
                             const brillo::SecureBlob& pass_blob,
                             brillo::SecureBlob* auth_value) override {
    return hwsec_foundation::error::CreateError<hwsec::TPMError>(
        "stub tpm operation", hwsec::TPMRetryAction::kNoRetry);
  }
  hwsec::Status GetEccAuthValue(std::optional<TpmKeyHandle> key_handle,
                                const brillo::SecureBlob& pass_blob,
                                brillo::SecureBlob* auth_value) override {
    return hwsec_foundation::error::CreateError<hwsec::TPMError>(
        "stub tpm operation", hwsec::TPMRetryAction::kNoRetry);
  }
  hwsec::CryptohomeFrontend* GetHwsec() override { return hwsec_.get(); }
  hwsec::PinWeaverFrontend* GetPinWeaver() override { return pinweaver_.get(); }

  hwsec::Status SealToPcrWithAuthorization(
      const SecureBlob& plaintext,
      const SecureBlob& auth_value,
      const std::map<uint32_t, brillo::Blob>& pcr_map,
      SecureBlob* sealed_data) override {
    return hwsec_foundation::error::CreateError<hwsec::TPMError>(
        "stub tpm operation", hwsec::TPMRetryAction::kNoRetry);
  }
  hwsec::Status PreloadSealedData(const SecureBlob& sealed_data,
                                  ScopedKeyHandle* preload_handle) override {
    return hwsec_foundation::error::CreateError<hwsec::TPMError>(
        "stub tpm operation", hwsec::TPMRetryAction::kNoRetry);
  }
  hwsec::Status UnsealWithAuthorization(
      std::optional<TpmKeyHandle> preload_handle,
      const SecureBlob& sealed_data,
      const SecureBlob& auth_value,
      const std::map<uint32_t, brillo::Blob>& pcr_map,
      SecureBlob* plaintext) override {
    return hwsec_foundation::error::CreateError<hwsec::TPMError>(
        "stub tpm operation", hwsec::TPMRetryAction::kNoRetry);
  }
  hwsec::Status GetPublicKeyHash(TpmKeyHandle key_handle,
                                 SecureBlob* hash) override {
    return nullptr;
  }
  bool IsEnabled() override { return false; }
  bool IsOwned() override { return false; }
  bool ReadNvram(uint32_t index, SecureBlob* blob) override { return false; }
  bool IsNvramDefined(uint32_t index) override { return false; }
  bool IsNvramLocked(uint32_t index) override { return false; }
  unsigned int GetNvramSize(uint32_t index) override { return 0; }
  hwsec::Status GetRandomDataBlob(size_t length, brillo::Blob* data) override {
    return hwsec_foundation::error::CreateError<hwsec::TPMError>(
        "stub tpm operation", hwsec::TPMRetryAction::kNoRetry);
  }
  hwsec::Status GetRandomDataSecureBlob(size_t length,
                                        brillo::SecureBlob* data) override {
    return hwsec_foundation::error::CreateError<hwsec::TPMError>(
        "stub tpm operation", hwsec::TPMRetryAction::kNoRetry);
  }
  bool DefineNvram(uint32_t index, size_t length, uint32_t flags) override {
    return false;
  }
  bool DestroyNvram(uint32_t index) override { return false; }
  bool WriteNvram(uint32_t index, const SecureBlob& blob) override {
    return false;
  }
  bool WriteLockNvram(uint32_t index) override { return false; }
  bool Sign(const SecureBlob& key_blob,
            const SecureBlob& der_encoded_input,
            uint32_t bound_pcr_index,
            SecureBlob* signature) override {
    return false;
  }
  bool CreatePCRBoundKey(const std::map<uint32_t, brillo::Blob>& pcr_map,
                         AsymmetricKeyUsage key_type,
                         SecureBlob* key_blob,
                         SecureBlob* public_key_der,
                         SecureBlob* creation_blob) override {
    return false;
  }
  bool VerifyPCRBoundKey(const std::map<uint32_t, brillo::Blob>& pcr_map,
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
  bool WrapRsaKey(const SecureBlob& public_modulus,
                  const SecureBlob& prime_factor,
                  SecureBlob* wrapped_key) override {
    return false;
  }
  bool CreateWrappedEccKey(SecureBlob* wrapped_key) override { return false; }
  hwsec::Status LoadWrappedKey(const SecureBlob& wrapped_key,
                               ScopedKeyHandle* key_handle) override {
    return hwsec_foundation::error::CreateError<hwsec::TPMError>(
        "stub tpm operation", hwsec::TPMRetryAction::kNoRetry);
  }
  void CloseHandle(TpmKeyHandle key_handle) override{};
  void GetStatus(std::optional<hwsec::Key> key,
                 TpmStatusInfo* status) override {}
  bool GetDictionaryAttackInfo(int* counter,
                               int* threshold,
                               bool* lockout,
                               int* seconds_remaining) override {
    return false;
  }
  bool ResetDictionaryAttackMitigation() override { return false; }
  void DeclareTpmFirmwareStable() override {}
  bool RemoveOwnerDependency(Tpm::TpmOwnerDependency dependency) override {
    return true;
  }
  bool GetVersionInfo(TpmVersionInfo* version_info) override { return false; }
  bool GetIFXFieldUpgradeInfo(IFXFieldUpgradeInfo* info) override {
    return false;
  }
  bool GetRsuDeviceId(std::string* device_id) override { return false; }
  SignatureSealingBackend* GetSignatureSealingBackend() override {
    return nullptr;
  }
  cryptorecovery::RecoveryCryptoTpmBackend* GetRecoveryCryptoBackend()
      override {
    return nullptr;
  }
  hwsec::Status IsDelegateBoundToPcr(bool* result) override {
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
  hwsec::Status IsSrkRocaVulnerable(bool*) override {
    return hwsec_foundation::error::CreateError<hwsec::TPMError>(
        "stub tpm operation", hwsec::TPMRetryAction::kNoRetry);
  }
  bool GetDelegate(brillo::Blob* blob,
                   brillo::Blob* secret,
                   bool* has_reset_lock_permissions) override {
    return false;
  }
  std::map<uint32_t, brillo::Blob> GetPcrMap(
      const std::string& obfuscated_username,
      bool use_extended_pcr) const override {
    return {};
  }

 private:
  std::unique_ptr<hwsec::Factory> hwsec_factory_;
  std::unique_ptr<hwsec::CryptohomeFrontend> hwsec_;
  std::unique_ptr<hwsec::PinWeaverFrontend> pinweaver_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STUB_TPM_H_
