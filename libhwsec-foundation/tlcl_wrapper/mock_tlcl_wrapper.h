// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_TLCL_WRAPPER_MOCK_TLCL_WRAPPER_H_
#define LIBHWSEC_FOUNDATION_TLCL_WRAPPER_MOCK_TLCL_WRAPPER_H_

#include <gmock/gmock.h>

#include <memory>

#include <brillo/secure_blob.h>

#include "libhwsec-foundation/hwsec-foundation_export.h"
#include "libhwsec-foundation/tlcl_wrapper/fake_tlcl_wrapper.h"

namespace hwsec_foundation {

class HWSEC_FOUNDATION_EXPORT MockTlclWrapper : public TlclWrapper {
 public:
  explicit MockTlclWrapper(std::unique_ptr<FakeTlclWrapper>);
  MockTlclWrapper() : MockTlclWrapper(std::make_unique<FakeTlclWrapper>()) {}

  MockTlclWrapper(const MockTlclWrapper&) = delete;
  MockTlclWrapper& operator=(const MockTlclWrapper&) = delete;

  MOCK_METHOD(uint32_t, Init, (), (override));

  MOCK_METHOD(uint32_t, Close, (), (override));

  MOCK_METHOD(uint32_t,
              Extend,
              (int pcr_num, const brillo::Blob&, brillo::Blob*),
              (override));

  MOCK_METHOD(uint32_t, GetOwnership, (bool*), (override));
  MOCK_METHOD(uint32_t, GetRandom, (uint8_t*, uint32_t, uint32_t*), (override));
  MOCK_METHOD(uint32_t,
              DefineSpace,
              (uint32_t, uint32_t, uint32_t),
              (override));

  MOCK_METHOD(uint32_t,
              DefineSpaceEx,
              (const uint8_t*,
               uint32_t,
               uint32_t,
               uint32_t,
               uint32_t,
               const void*,
               uint32_t),
              (override));

  MOCK_METHOD(uint32_t, GetPermissions, (uint32_t, uint32_t*), (override));

  MOCK_METHOD(uint32_t,
              GetSpaceInfo,
              (uint32_t, uint32_t*, uint32_t*, void*, uint32_t*),
              (override));

  MOCK_METHOD(uint32_t, Write, (uint32_t, const void*, uint32_t), (override));

  MOCK_METHOD(uint32_t, Read, (uint32_t, void*, uint32_t), (override));

  MOCK_METHOD(uint32_t, WriteLock, (uint32_t), (override));

  MOCK_METHOD(uint32_t, ReadLock, (uint32_t), (override));

  MOCK_METHOD(uint32_t, PCRRead, (uint32_t, void*, uint32_t), (override));

  MOCK_METHOD(uint32_t,
              InitNvAuthPolicy,
              (uint32_t, const uint8_t[][TPM_PCR_DIGEST], void*, uint32_t*),
              (override));

  MOCK_METHOD(uint32_t,
              GetVersion,
              (uint32_t*, uint64_t*, uint8_t*, size_t*),
              (override));

  MOCK_METHOD(uint32_t,
              IFXFieldUpgradeInfo,
              (TPM_IFX_FIELDUPGRADEINFO*),
              (override));

#if !USE_TPM2

  MOCK_METHOD(uint32_t,
              ReadPubek,
              (uint32_t*, uint8_t*, uint32_t*),
              (override));

  MOCK_METHOD(uint32_t,
              TakeOwnership,
              (const brillo::SecureBlob&,
               const brillo::SecureBlob&,
               const brillo::SecureBlob&),
              (override));

  MOCK_METHOD(uint32_t, CreateDelegationFamily, (uint8_t), (override));

  MOCK_METHOD(uint32_t,
              ReadDelegationFamilyTable,
              (TPM_FAMILY_TABLE_ENTRY*, uint32_t*),
              (override));

#endif  // !USE_TPM2
  FakeTlclWrapper* GetFake() { return fake_tlcl_wrapper_.get(); }

 private:
  std::unique_ptr<FakeTlclWrapper> fake_tlcl_wrapper_;
};

}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_TLCL_WRAPPER_MOCK_TLCL_WRAPPER_H_
