// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_FUZZERS_FUZZED_TPM_MANAGER_UTILITY_H_
#define CHAPS_FUZZERS_FUZZED_TPM_MANAGER_UTILITY_H_

#include <fuzzer/FuzzedDataProvider.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <string>
#include <vector>

#include "tpm_manager/client/tpm_manager_utility.h"

namespace chaps {

class FuzzedTpmManagerUtility : public tpm_manager::TpmManagerUtility {
 public:
  explicit FuzzedTpmManagerUtility(FuzzedDataProvider* data_provider);
  FuzzedTpmManagerUtility(const FuzzedTpmManagerUtility&) = delete;
  FuzzedTpmManagerUtility& operator=(const FuzzedTpmManagerUtility&) = delete;

  ~FuzzedTpmManagerUtility() override{};

  bool Initialize() override;

  bool TakeOwnership() override;

  bool GetTpmStatus(bool* is_enabled,
                    bool* is_owned,
                    tpm_manager::LocalData* local_data) override;

  bool GetTpmNonsensitiveStatus(bool* is_enabled,
                                bool* is_owned,
                                bool* is_owner_password_present,
                                bool* has_reset_lock_permissions) override;

  bool GetVersionInfo(uint32_t* family,
                      uint64_t* spec_level,
                      uint32_t* manufacturer,
                      uint32_t* tpm_model,
                      uint64_t* firmware_version,
                      std::string* vendor_specific) override;

  bool RemoveOwnerDependency(const std::string& dependency) override;

  bool ClearStoredOwnerPassword() override;

  bool GetDictionaryAttackInfo(int* counter,
                               int* threshold,
                               bool* lockout,
                               int* seconds_remaining) override;

  bool ResetDictionaryAttackLock() override;

  bool DefineSpace(uint32_t index,
                   size_t size,
                   bool write_define,
                   bool bind_to_pcr0,
                   bool firmware_readable) override;

  bool DestroySpace(uint32_t index) override;

  bool WriteSpace(uint32_t index,
                  const std::string& data,
                  bool use_owner_auth) override;

  bool ReadSpace(uint32_t index,
                 bool use_owner_auth,
                 std::string* output) override;

  bool ListSpaces(std::vector<uint32_t>* spaces) override;

  bool GetSpaceInfo(
      uint32_t index,
      uint32_t* size,
      bool* is_read_locked,
      bool* is_write_locked,
      std::vector<tpm_manager::NvramSpaceAttribute>* attributes) override;

  bool LockSpace(uint32_t index) override;

  bool GetOwnershipTakenSignalStatus(
      bool* is_successful,
      bool* has_received,
      tpm_manager::LocalData* local_data) override;

  void AddOwnershipCallback(OwnershipCallback ownership_callback) override;

  void OnOwnershipTaken(
      const tpm_manager::OwnershipTakenSignal& signal) override;

  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool is_successful) override;

 private:
  FuzzedDataProvider* data_provider_;
};

}  // namespace chaps

#endif  // CHAPS_FUZZERS_FUZZED_TPM_MANAGER_UTILITY_H_
