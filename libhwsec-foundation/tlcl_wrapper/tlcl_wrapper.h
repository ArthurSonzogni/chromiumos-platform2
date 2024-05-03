// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_TLCL_WRAPPER_TLCL_WRAPPER_H_
#define LIBHWSEC_FOUNDATION_TLCL_WRAPPER_TLCL_WRAPPER_H_

#include <stdint.h>

#include <brillo/secure_blob.h>
#include <vboot/tss_constants.h>

#include "libhwsec-foundation/hwsec-foundation_export.h"

namespace hwsec_foundation {

// TlclWrapper is a wrapper around the vboot tlcl library so that we can
// mock TPM access.
class HWSEC_FOUNDATION_EXPORT TlclWrapper {
 public:
  TlclWrapper() = default;

  virtual ~TlclWrapper() = default;

  virtual uint32_t Init() = 0;

  virtual uint32_t Close() = 0;

  virtual uint32_t Extend(int pcr_num,
                          const brillo::Blob& in_digest,
                          brillo::Blob* out_digest) = 0;

  virtual uint32_t GetOwnership(bool* owned) = 0;

  virtual uint32_t GetRandom(uint8_t* data,
                             uint32_t length,
                             uint32_t* size) = 0;

  virtual uint32_t DefineSpace(uint32_t index,
                               uint32_t perm,
                               uint32_t size) = 0;

  virtual uint32_t DefineSpaceEx(const uint8_t* owner_auth,
                                 uint32_t owner_auth_size,
                                 uint32_t index,
                                 uint32_t perm,
                                 uint32_t size,
                                 const void* auth_policy,
                                 uint32_t auth_policy_size) = 0;

  virtual uint32_t GetPermissions(uint32_t index, uint32_t* permissions) = 0;

  virtual uint32_t GetSpaceInfo(uint32_t index,
                                uint32_t* attributes,
                                uint32_t* size,
                                void* auth_policy,
                                uint32_t* auth_policy_size) = 0;

  virtual uint32_t Write(uint32_t index, const void* data, uint32_t length) = 0;

  virtual uint32_t Read(uint32_t index, void* data, uint32_t length) = 0;

  virtual uint32_t WriteLock(uint32_t index) = 0;

  virtual uint32_t ReadLock(uint32_t index) = 0;

  virtual uint32_t PCRRead(uint32_t index, void* data, uint32_t length) = 0;

  virtual uint32_t InitNvAuthPolicy(uint32_t pcr_selection_bitmap,
                                    const uint8_t pcr_values[][TPM_PCR_DIGEST],
                                    void* auth_policy,
                                    uint32_t* auth_policy_size) = 0;

  virtual uint32_t GetVersion(uint32_t* vendor,
                              uint64_t* firmware_version,
                              uint8_t* vendor_specific_buf,
                              size_t* vendor_specific_buf_size) = 0;

  virtual uint32_t IFXFieldUpgradeInfo(TPM_IFX_FIELDUPGRADEINFO* info) = 0;

#if !USE_TPM2

  virtual uint32_t ReadPubek(uint32_t* public_exponent,
                             uint8_t* modulus,
                             uint32_t* modulus_size) = 0;

  virtual uint32_t TakeOwnership(const brillo::SecureBlob& enc_owner_auth,
                                 const brillo::SecureBlob& enc_srk_auth,
                                 const brillo::SecureBlob& owner_auth) = 0;

  virtual uint32_t CreateDelegationFamily(uint8_t family_label) = 0;

  virtual uint32_t ReadDelegationFamilyTable(TPM_FAMILY_TABLE_ENTRY* table,
                                             uint32_t* table_size) = 0;

#endif  // !USE_TPM2
};

}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_TLCL_WRAPPER_TLCL_WRAPPER_H_
