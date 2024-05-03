// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_TLCL_WRAPPER_FAKE_TLCL_WRAPPER_H_
#define LIBHWSEC_FOUNDATION_TLCL_WRAPPER_FAKE_TLCL_WRAPPER_H_

#include <stdint.h>

#include <map>
#include <memory>
#include <vector>

#include <brillo/secure_blob.h>

#include "libhwsec-foundation/hwsec-foundation_export.h"
#include "libhwsec-foundation/tlcl_wrapper/tlcl_wrapper.h"

namespace hwsec_foundation {

// FakeTlclWrapper emulare the tlcl library.
class HWSEC_FOUNDATION_EXPORT FakeTlclWrapper : public TlclWrapper {
 public:
  struct NvramSpaceData {
    uint32_t attributes = 0;
    std::vector<uint8_t> policy;
    brillo::SecureBlob contents;
    bool write_locked = false;
    bool read_locked = false;
  };

  FakeTlclWrapper() {}
  ~FakeTlclWrapper() override = default;

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

  // Get the space data for |index|.
  NvramSpaceData* GetSpace(uint32_t index) { return &nvram_spaces_[index]; }

  // Put the TPM into owned state with the specified owner auth secret.
  void SetOwned(const brillo::SecureBlob& owner_auth) {
    owner_auth_ = owner_auth;
  }

  // Returns the ownership flag.
  bool IsOwned() const { return !owner_auth_.empty(); }

  // Clear the TPM owner.
  void Clear();

  // Reset the TPM (i.e. what happens at reboot).
  void Reset();

  // Configure a PCR to contain the specified value.
  void SetPCRValue(uint32_t index, const uint8_t value[TPM_PCR_DIGEST]);

  int GetDictionaryAttackCounter() { return dictionary_attack_counter_; }

 private:
  template <typename Action>
  uint32_t WithSpace(uint32_t index, Action action);

  brillo::SecureBlob owner_auth_;
  std::map<uint32_t, NvramSpaceData> nvram_spaces_;
  std::map<uint32_t, uint8_t[TPM_PCR_DIGEST]> pcr_values_;

#if !USE_TPM2
  uint32_t delegation_family_id_ = 0;
  std::vector<TPM_FAMILY_TABLE_ENTRY> delegation_family_table_;
#endif  // !USE_TPM2

  // The emulated dictionary attack counter.
  int dictionary_attack_counter_ = 0;
};

}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_TLCL_WRAPPER_FAKE_TLCL_WRAPPER_H_
