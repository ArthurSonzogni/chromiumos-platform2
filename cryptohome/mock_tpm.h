// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_TPM_H_
#define CRYPTOHOME_MOCK_TPM_H_

#include "cryptohome/tpm.h"

#include <stdint.h>

#include <map>
#include <set>
#include <string>

#include <base/logging.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

namespace cryptohome {

class MockTpm : public Tpm {
 public:
  MockTpm();
  ~MockTpm();
  MOCK_METHOD(TpmVersion, GetVersion, (), (override));
  MOCK_METHOD(hwsec::error::TPMErrorBase,
              EncryptBlob,
              (TpmKeyHandle,
               const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               brillo::SecureBlob*),
              (override));
  MOCK_METHOD(hwsec::error::TPMErrorBase,
              DecryptBlob,
              (TpmKeyHandle,
               const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               (const std::map<uint32_t, std::string>&),
               brillo::SecureBlob*),
              (override));
  MOCK_METHOD(hwsec::error::TPMErrorBase,
              SealToPcrWithAuthorization,
              (const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               (const std::map<uint32_t, std::string>&),
               brillo::SecureBlob*),
              (override));
  MOCK_METHOD(hwsec::error::TPMErrorBase,
              PreloadSealedData,
              (const brillo::SecureBlob&, ScopedKeyHandle*),
              (override));
  MOCK_METHOD(hwsec::error::TPMErrorBase,
              UnsealWithAuthorization,
              (base::Optional<TpmKeyHandle>,
               const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               (const std::map<uint32_t, std::string>&),
               brillo::SecureBlob*),
              (override));
  MOCK_METHOD(hwsec::error::TPMErrorBase,
              GetPublicKeyHash,
              (TpmKeyHandle, brillo::SecureBlob*),
              (override));
  MOCK_METHOD(bool, IsEnabled, (), (override));
  MOCK_METHOD(bool, IsOwned, (), (override));
  MOCK_METHOD(bool, IsOwnerPasswordPresent, (), (override));
  MOCK_METHOD(bool, HasResetLockPermissions, (), (override));
  MOCK_METHOD(hwsec::error::TPMErrorBase,
              GetRandomDataBlob,
              (size_t, brillo::Blob*),
              (override));
  MOCK_METHOD(hwsec::error::TPMErrorBase,
              GetRandomDataSecureBlob,
              (size_t, brillo::SecureBlob*),
              (override));
  MOCK_METHOD(hwsec::error::TPMErrorBase,
              GetAlertsData,
              (Tpm::AlertsData*),
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
              SealToPCR0,
              (const brillo::SecureBlob&, brillo::SecureBlob*),
              (override));
  MOCK_METHOD(bool,
              Unseal,
              (const brillo::SecureBlob&, brillo::SecureBlob*),
              (override));
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
  MOCK_METHOD(bool, ExtendPCR, (uint32_t, const brillo::Blob&), (override));
  MOCK_METHOD(bool, ReadPCR, (uint32_t, brillo::Blob*), (override));
  MOCK_METHOD(bool,
              CreatePCRBoundKey,
              ((const std::map<uint32_t, std::string>&),
               AsymmetricKeyUsage,
               brillo::SecureBlob*,
               brillo::SecureBlob*,
               brillo::SecureBlob*),
              (override));
  MOCK_METHOD(bool,
              VerifyPCRBoundKey,
              ((const std::map<uint32_t, std::string>&),
               const brillo::SecureBlob&,
               const brillo::SecureBlob&),
              (override));
  MOCK_METHOD(bool, IsEndorsementKeyAvailable, (), (override));
  MOCK_METHOD(bool, CreateEndorsementKey, (), (override));
  MOCK_METHOD(bool,
              TakeOwnership,
              (int, const brillo::SecureBlob&),
              (override));
  MOCK_METHOD(bool,
              WrapRsaKey,
              (const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               brillo::SecureBlob*),
              (override));
  MOCK_METHOD(hwsec::error::TPMErrorBase,
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
              (base::Optional<TpmKeyHandle>, TpmStatusInfo*),
              (override));
  MOCK_METHOD(hwsec::error::TPMErrorBase,
              IsSrkRocaVulnerable,
              (bool*),
              (override));
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
  MOCK_METHOD(bool, ClearStoredPassword, (), (override));
  MOCK_METHOD(bool, GetVersionInfo, (TpmVersionInfo*), (override));
  MOCK_METHOD(bool, GetIFXFieldUpgradeInfo, (IFXFieldUpgradeInfo*), (override));
  MOCK_METHOD(bool, GetRsuDeviceId, (std::string*), (override));
  MOCK_METHOD(LECredentialBackend*, GetLECredentialBackend, (), (override));
  MOCK_METHOD(SignatureSealingBackend*,
              GetSignatureSealingBackend,
              (),
              (override));
  MOCK_METHOD(bool,
              GetDelegate,
              (brillo::Blob*, brillo::Blob*, bool*),
              (override));
  MOCK_METHOD2(SetDelegateData, void(const std::string&, bool));
  MOCK_METHOD(hwsec::error::TPMErrorBase,
              IsDelegateBoundToPcr,
              (bool*),
              (override));
  MOCK_METHOD0(DelegateCanResetDACounter, bool());
  MOCK_METHOD((std::map<uint32_t, std::string>),
              GetPcrMap,
              (const std::string&, bool),
              (const override));
  MOCK_METHOD(hwsec::error::TPMErrorBase,
              GetAuthValue,
              (base::Optional<TpmKeyHandle> key_handle,
               const brillo::SecureBlob& pass_blob,
               brillo::SecureBlob* auth_value),
              (override));

 private:
  hwsec::error::TPMErrorBase XorDecrypt(
      TpmKeyHandle _key,
      const brillo::SecureBlob& plaintext,
      const brillo::SecureBlob& key,
      const std::map<uint32_t, std::string>& pcr_map,
      brillo::SecureBlob* ciphertext) {
    return Xor(_key, plaintext, key, ciphertext);
  }
  hwsec::error::TPMErrorBase Xor(TpmKeyHandle _key,
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

  hwsec::error::TPMErrorBase FakeGetRandomDataBlob(size_t num_bytes,
                                                   brillo::Blob* blob) {
    blob->resize(num_bytes);
    return nullptr;
  }

  hwsec::error::TPMErrorBase FakeGetRandomDataSecureBlob(
      size_t num_bytes, brillo::SecureBlob* sblob) {
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
