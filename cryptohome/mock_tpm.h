// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_TPM_H_
#define CRYPTOHOME_MOCK_TPM_H_

#include "cryptohome/tpm.h"

#include <stdint.h>

#include <map>
#include <optional>
#include <set>
#include <string>

#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <libhwsec/status.h>
#include <gmock/gmock.h>

namespace cryptohome {

class MockTpm : public Tpm {
 public:
  MockTpm();
  ~MockTpm();
  MOCK_METHOD(TpmVersion, GetVersion, (), (override));
  MOCK_METHOD(hwsec::Status,
              EncryptBlob,
              (TpmKeyHandle,
               const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               brillo::SecureBlob*),
              (override));
  MOCK_METHOD(hwsec::Status,
              DecryptBlob,
              (TpmKeyHandle,
               const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               brillo::SecureBlob*),
              (override));
  MOCK_METHOD(hwsec::Status,
              SealToPcrWithAuthorization,
              (const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               (const std::map<uint32_t, brillo::Blob>&),
               brillo::SecureBlob*),
              (override));
  MOCK_METHOD(hwsec::Status,
              PreloadSealedData,
              (const brillo::SecureBlob&, ScopedKeyHandle*),
              (override));
  MOCK_METHOD(hwsec::Status,
              UnsealWithAuthorization,
              (std::optional<TpmKeyHandle>,
               const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               (const std::map<uint32_t, brillo::Blob>&),
               brillo::SecureBlob*),
              (override));
  MOCK_METHOD(hwsec::Status,
              GetPublicKeyHash,
              (TpmKeyHandle, brillo::SecureBlob*),
              (override));
  MOCK_METHOD(bool, IsEnabled, (), (override));
  MOCK_METHOD(bool, IsOwned, (), (override));
  MOCK_METHOD(bool, IsOwnerPasswordPresent, (), (override));
  MOCK_METHOD(bool, HasResetLockPermissions, (), (override));
  MOCK_METHOD(hwsec::Status,
              GetRandomDataBlob,
              (size_t, brillo::Blob*),
              (override));
  MOCK_METHOD(hwsec::Status,
              GetRandomDataSecureBlob,
              (size_t, brillo::SecureBlob*),
              (override));
  MOCK_METHOD(bool, DefineNvram, (uint32_t, size_t, uint32_t), (override));
  MOCK_METHOD(bool,
              WriteNvram,
              (uint32_t, const brillo::SecureBlob&),
              (override));
  MOCK_METHOD(bool,
              OwnerWriteNvram,
              (uint32_t, const brillo::SecureBlob&),
              (override));
  MOCK_METHOD(bool, ReadNvram, (uint32_t, brillo::SecureBlob*), (override));
  MOCK_METHOD(bool, DestroyNvram, (uint32_t), (override));
  MOCK_METHOD(bool, IsNvramDefined, (uint32_t), (override));
  MOCK_METHOD(bool, IsNvramLocked, (uint32_t), (override));
  MOCK_METHOD(bool, WriteLockNvram, (uint32_t), (override));
  MOCK_METHOD(unsigned int, GetNvramSize, (uint32_t), (override));
  MOCK_METHOD(bool,
              CreateDelegate,
              (const std::set<uint32_t>&,
               uint8_t,
               uint8_t,
               brillo::Blob*,
               brillo::Blob*),
              (override));
  MOCK_METHOD(bool,
              Sign,
              (const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               uint32_t,
               brillo::SecureBlob*),
              (override));
  MOCK_METHOD(bool,
              CreatePCRBoundKey,
              ((const std::map<uint32_t, brillo::Blob>&),
               AsymmetricKeyUsage,
               brillo::SecureBlob*,
               brillo::SecureBlob*,
               brillo::SecureBlob*),
              (override));
  MOCK_METHOD(bool,
              VerifyPCRBoundKey,
              ((const std::map<uint32_t, brillo::Blob>&),
               const brillo::SecureBlob&,
               const brillo::SecureBlob&),
              (override));
  MOCK_METHOD(bool, ExtendPCR, (uint32_t, const brillo::Blob&), (override));
  MOCK_METHOD(bool, ReadPCR, (uint32_t, brillo::Blob*), (override));
  MOCK_METHOD(bool,
              WrapRsaKey,
              (const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               brillo::SecureBlob*),
              (override));
  MOCK_METHOD(bool, CreateWrappedEccKey, (brillo::SecureBlob*), (override));
  MOCK_METHOD(hwsec::Status,
              LoadWrappedKey,
              (const brillo::SecureBlob&, ScopedKeyHandle*),
              (override));
  MOCK_METHOD(bool,
              LegacyLoadCryptohomeKey,
              (ScopedKeyHandle*, brillo::SecureBlob*),
              (override));
  MOCK_METHOD(void, CloseHandle, (TpmKeyHandle), (override));
  MOCK_METHOD(void,
              GetStatus,
              (std::optional<TpmKeyHandle>, TpmStatusInfo*),
              (override));
  MOCK_METHOD(hwsec::Status, IsSrkRocaVulnerable, (bool*), (override));
  MOCK_METHOD(bool,
              GetDictionaryAttackInfo,
              (int*, int*, bool*, int*),
              (override));
  MOCK_METHOD(bool,
              ResetDictionaryAttackMitigation,
              (const brillo::Blob&, const brillo::Blob&),
              (override));
  MOCK_METHOD(void, DeclareTpmFirmwareStable, (), (override));
  MOCK_METHOD(bool,
              RemoveOwnerDependency,
              (Tpm::TpmOwnerDependency),
              (override));
  MOCK_METHOD(bool, GetVersionInfo, (TpmVersionInfo*), (override));
  MOCK_METHOD(bool, GetIFXFieldUpgradeInfo, (IFXFieldUpgradeInfo*), (override));
  MOCK_METHOD(bool, GetRsuDeviceId, (std::string*), (override));
  MOCK_METHOD(LECredentialBackend*, GetLECredentialBackend, (), (override));
  MOCK_METHOD(SignatureSealingBackend*,
              GetSignatureSealingBackend,
              (),
              (override));
  MOCK_METHOD(cryptorecovery::RecoveryCryptoTpmBackend*,
              GetRecoveryCryptoBackend,
              (),
              (override));
  MOCK_METHOD(bool,
              GetDelegate,
              (brillo::Blob*, brillo::Blob*, bool*),
              (override));
  MOCK_METHOD2(SetDelegateData, void(const std::string&, bool));
  MOCK_METHOD(hwsec::Status, IsDelegateBoundToPcr, (bool*), (override));
  MOCK_METHOD0(DelegateCanResetDACounter, bool());
  MOCK_METHOD((std::map<uint32_t, brillo::Blob>),
              GetPcrMap,
              (const std::string&, bool),
              (const override));
  MOCK_METHOD(hwsec::Status,
              GetAuthValue,
              (std::optional<TpmKeyHandle> key_handle,
               const brillo::SecureBlob& pass_blob,
               brillo::SecureBlob* auth_value),
              (override));
  MOCK_METHOD(hwsec::Status,
              GetEccAuthValue,
              (std::optional<TpmKeyHandle> key_handle,
               const brillo::SecureBlob& pass_blob,
               brillo::SecureBlob* auth_value),
              (override));

 private:
  hwsec::Status XorDecrypt(TpmKeyHandle _key,
                           const brillo::SecureBlob& plaintext,
                           const brillo::SecureBlob& key,
                           brillo::SecureBlob* ciphertext) {
    return Xor(_key, plaintext, key, ciphertext);
  }
  hwsec::Status Xor(TpmKeyHandle _key,
                    const brillo::SecureBlob& plaintext,
                    const brillo::SecureBlob& key,
                    brillo::SecureBlob* ciphertext) {
    brillo::SecureBlob local_data_out(plaintext.size());
    for (unsigned int i = 0; i < local_data_out.size(); i++) {
      local_data_out[i] = plaintext[i] ^ 0x1e;
    }
    ciphertext->swap(local_data_out);
    return nullptr;
  }

  hwsec::Status FakeGetRandomDataBlob(size_t num_bytes, brillo::Blob* blob) {
    blob->resize(num_bytes);
    return nullptr;
  }

  hwsec::Status FakeGetRandomDataSecureBlob(size_t num_bytes,
                                            brillo::SecureBlob* sblob) {
    sblob->resize(num_bytes);
    return nullptr;
  }

  bool FakeExtendPCR(uint32_t index, const brillo::Blob& value) {
    extended_pcrs_.insert(index);
    return true;
  }

  bool FakeReadPCR(uint32_t index, brillo::Blob* value) {
    value->assign(20, extended_pcrs_.count(index) ? 0xAA : 0);
    return true;
  }

  std::set<uint32_t> extended_pcrs_;
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_TPM_H_
