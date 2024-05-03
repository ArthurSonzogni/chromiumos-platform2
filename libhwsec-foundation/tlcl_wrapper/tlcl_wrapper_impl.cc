// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec-foundation/tlcl_wrapper/tlcl_wrapper_impl.h"

#include <brillo/secure_blob.h>
#include <openssl/sha.h>
#include <vboot/tlcl.h>

namespace hwsec_foundation {

uint32_t TlclWrapperImpl::Init() {
#if USE_TPM_DYNAMIC
  // tlcl doesn't support TPM dynamic.
  return 1;
#else
  return TlclLibInit();
#endif
}

uint32_t TlclWrapperImpl::Close() {
  return TlclLibClose();
}

uint32_t TlclWrapperImpl::Extend(int pcr_num,
                                 const brillo::Blob& in_digest,
                                 brillo::Blob* out_digest) {
  uint8_t out_buffer[TPM_PCR_DIGEST];
  memset(out_buffer, 0, TPM_PCR_DIGEST);
  uint32_t result = TlclExtend(pcr_num, in_digest.data(), out_buffer);
  if (out_digest) {
    *out_digest = brillo::Blob(out_buffer, out_buffer + TPM_PCR_DIGEST);
  }
  return result;
}

uint32_t TlclWrapperImpl::GetOwnership(bool* owned) {
  uint8_t owned_out = 0;
  uint32_t result = TlclGetOwnership(&owned_out);
  if (owned) {
    *owned = static_cast<bool>(owned_out);
  }
  return result;
}

uint32_t TlclWrapperImpl::GetRandom(uint8_t* data,
                                    uint32_t length,
                                    uint32_t* size) {
  return TlclGetRandom(data, length, size);
}

uint32_t TlclWrapperImpl::DefineSpace(uint32_t index,
                                      uint32_t perm,
                                      uint32_t size) {
  return TlclDefineSpace(index, perm, size);
}

uint32_t TlclWrapperImpl::DefineSpaceEx(const uint8_t* owner_auth,
                                        uint32_t owner_auth_size,
                                        uint32_t index,
                                        uint32_t perm,
                                        uint32_t size,
                                        const void* auth_policy,
                                        uint32_t auth_policy_size) {
  return TlclDefineSpaceEx(owner_auth, owner_auth_size, index, perm, size,
                           auth_policy, auth_policy_size);
}

uint32_t TlclWrapperImpl::GetPermissions(uint32_t index,
                                         uint32_t* permissions) {
  return TlclGetPermissions(index, permissions);
}

uint32_t TlclWrapperImpl::GetSpaceInfo(uint32_t index,
                                       uint32_t* attributes,
                                       uint32_t* size,
                                       void* auth_policy,
                                       uint32_t* auth_policy_size) {
  return TlclGetSpaceInfo(index, attributes, size, auth_policy,
                          auth_policy_size);
}

uint32_t TlclWrapperImpl::Write(uint32_t index,
                                const void* data,
                                uint32_t length) {
  return TlclWrite(index, data, length);
}

uint32_t TlclWrapperImpl::Read(uint32_t index, void* data, uint32_t length) {
  return TlclRead(index, data, length);
}

uint32_t TlclWrapperImpl::WriteLock(uint32_t index) {
  return TlclWriteLock(index);
}

uint32_t TlclWrapperImpl::ReadLock(uint32_t index) {
  return TlclReadLock(index);
}

uint32_t TlclWrapperImpl::PCRRead(uint32_t index, void* data, uint32_t length) {
  return TlclPCRRead(index, data, length);
}

uint32_t TlclWrapperImpl::InitNvAuthPolicy(
    uint32_t pcr_selection_bitmap,
    const uint8_t pcr_values[][TPM_PCR_DIGEST],
    void* auth_policy,
    uint32_t* auth_policy_size) {
  return TlclInitNvAuthPolicy(pcr_selection_bitmap, pcr_values, auth_policy,
                              auth_policy_size);
}

uint32_t TlclWrapperImpl::GetVersion(uint32_t* vendor,
                                     uint64_t* firmware_version,
                                     uint8_t* vendor_specific_buf,
                                     size_t* vendor_specific_buf_size) {
  return TlclGetVersion(vendor, firmware_version, vendor_specific_buf,
                        vendor_specific_buf_size);
}

uint32_t TlclWrapperImpl::IFXFieldUpgradeInfo(TPM_IFX_FIELDUPGRADEINFO* info) {
  return TlclIFXFieldUpgradeInfo(info);
}

#if !USE_TPM2

uint32_t TlclWrapperImpl::ReadPubek(uint32_t* public_exponent,
                                    uint8_t* modulus,
                                    uint32_t* modulus_size) {
  return TlclReadPubek(public_exponent, modulus, modulus_size);
}

uint32_t TlclWrapperImpl::TakeOwnership(
    const brillo::SecureBlob& enc_owner_auth,
    const brillo::SecureBlob& enc_srk_auth,
    const brillo::SecureBlob& owner_auth) {
  return TlclTakeOwnership(enc_owner_auth.data(), enc_srk_auth.data(),
                           owner_auth.data());
}

uint32_t TlclWrapperImpl::CreateDelegationFamily(uint8_t family_label) {
  return TlclCreateDelegationFamily(family_label);
}

uint32_t TlclWrapperImpl::ReadDelegationFamilyTable(
    TPM_FAMILY_TABLE_ENTRY* table, uint32_t* table_size) {
  return TlclReadDelegationFamilyTable(table, table_size);
}

#endif  // !USE_TPM2

}  // namespace hwsec_foundation
