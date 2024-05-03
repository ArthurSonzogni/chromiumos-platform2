// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_TLCL_WRAPPER_TLCL_WRAPPER_IMPL_H_
#define LIBHWSEC_FOUNDATION_TLCL_WRAPPER_TLCL_WRAPPER_IMPL_H_

#include <stdint.h>

#include <brillo/secure_blob.h>

#include "libhwsec-foundation/hwsec-foundation_export.h"
#include "libhwsec-foundation/tlcl_wrapper/tlcl_wrapper.h"

namespace hwsec_foundation {

// TlclWrapperImpl call the tlcl library.
class HWSEC_FOUNDATION_EXPORT TlclWrapperImpl : public TlclWrapper {
 public:
  TlclWrapperImpl() = default;
  ~TlclWrapperImpl() override = default;

  uint32_t Init() override;
  uint32_t Close() override;
  uint32_t Extend(int pcr_num,
                  const brillo::Blob& in_digest,
                  brillo::Blob* out_digest) override;
  uint32_t GetOwnership(bool* owned) override;
  uint32_t GetRandom(uint8_t* data, uint32_t length, uint32_t* size) override;
  uint32_t DefineSpace(uint32_t index, uint32_t perm, uint32_t size) override;

  uint32_t DefineSpaceEx(const uint8_t* owner_auth,
                         uint32_t owner_auth_size,
                         uint32_t index,
                         uint32_t perm,
                         uint32_t size,
                         const void* auth_policy,
                         uint32_t auth_policy_size) override;

  uint32_t GetPermissions(uint32_t index, uint32_t* permissions) override;

  uint32_t GetSpaceInfo(uint32_t index,
                        uint32_t* attributes,
                        uint32_t* size,
                        void* auth_policy,
                        uint32_t* auth_policy_size) override;

  uint32_t Write(uint32_t index, const void* data, uint32_t length) override;

  uint32_t Read(uint32_t index, void* data, uint32_t length) override;

  uint32_t WriteLock(uint32_t index) override;

  uint32_t ReadLock(uint32_t index) override;

  uint32_t PCRRead(uint32_t index, void* data, uint32_t length) override;

  uint32_t InitNvAuthPolicy(uint32_t pcr_selection_bitmap,
                            const uint8_t pcr_values[][TPM_PCR_DIGEST],
                            void* auth_policy,
                            uint32_t* auth_policy_size) override;

  uint32_t GetVersion(uint32_t* vendor,
                      uint64_t* firmware_version,
                      uint8_t* vendor_specific_buf,
                      size_t* vendor_specific_buf_size) override;

  uint32_t IFXFieldUpgradeInfo(TPM_IFX_FIELDUPGRADEINFO* info) override;

#if !USE_TPM2

  uint32_t ReadPubek(uint32_t* public_exponent,
                     uint8_t* modulus,
                     uint32_t* modulus_size) override;

  uint32_t TakeOwnership(const brillo::SecureBlob& enc_owner_auth,
                         const brillo::SecureBlob& enc_srk_auth,
                         const brillo::SecureBlob& owner_auth) override;

  uint32_t CreateDelegationFamily(uint8_t family_label) override;

  uint32_t ReadDelegationFamilyTable(TPM_FAMILY_TABLE_ENTRY* table,
                                     uint32_t* table_size) override;

#endif  // !USE_TPM2
};

}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_TLCL_WRAPPER_TLCL_WRAPPER_IMPL_H_
