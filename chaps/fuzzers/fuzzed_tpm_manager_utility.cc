// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/fuzzers/fuzzed_tpm_manager_utility.h"

#include <fuzzer/FuzzedDataProvider.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <string>
#include <vector>

namespace chaps {

FuzzedTpmManagerUtility::FuzzedTpmManagerUtility(
    FuzzedDataProvider* data_provider)
    : data_provider_(data_provider) {}

bool FuzzedTpmManagerUtility::Initialize() {
  return false;
}

bool FuzzedTpmManagerUtility::TakeOwnership() {
  return false;
}

bool FuzzedTpmManagerUtility::GetTpmStatus(bool* is_enabled,
                                           bool* is_owned,
                                           tpm_manager::LocalData* local_data) {
  return false;
}

bool FuzzedTpmManagerUtility::GetTpmNonsensitiveStatus(
    bool* is_enabled,
    bool* is_owned,
    bool* is_owner_password_present,
    bool* has_reset_lock_permissions) {
  if (data_provider_->ConsumeBool()) {
    return false;
  } else {
    // Only is_owned and is_enabled are checked in TPM2UtilityImpl.
    *is_owned = data_provider_->ConsumeBool();
    *is_enabled = data_provider_->ConsumeBool();
    return true;
  }
}

bool FuzzedTpmManagerUtility::GetVersionInfo(uint32_t* family,
                                             uint64_t* spec_level,
                                             uint32_t* manufacturer,
                                             uint32_t* tpm_model,
                                             uint64_t* firmware_version,
                                             std::string* vendor_specific) {
  return false;
}

bool FuzzedTpmManagerUtility::RemoveOwnerDependency(
    const std::string& dependency) {
  return false;
}

bool FuzzedTpmManagerUtility::ClearStoredOwnerPassword() {
  return false;
}

bool FuzzedTpmManagerUtility::GetDictionaryAttackInfo(int* counter,
                                                      int* threshold,
                                                      bool* lockout,
                                                      int* seconds_remaining) {
  return false;
}

bool FuzzedTpmManagerUtility::ResetDictionaryAttackLock() {
  return false;
}

bool FuzzedTpmManagerUtility::DefineSpace(uint32_t index,
                                          size_t size,
                                          bool write_define,
                                          bool bind_to_pcr0,
                                          bool firmware_readable) {
  return false;
}

bool FuzzedTpmManagerUtility::DestroySpace(uint32_t index) {
  return false;
}

bool FuzzedTpmManagerUtility::WriteSpace(uint32_t index,
                                         const std::string& data,
                                         bool use_owner_auth) {
  return false;
}

bool FuzzedTpmManagerUtility::ReadSpace(uint32_t index,
                                        bool use_owner_auth,
                                        std::string* output) {
  return false;
}

bool FuzzedTpmManagerUtility::ListSpaces(std::vector<uint32_t>* spaces) {
  return false;
}

bool FuzzedTpmManagerUtility::GetSpaceInfo(
    uint32_t index,
    uint32_t* size,
    bool* is_read_locked,
    bool* is_write_locked,
    std::vector<tpm_manager::NvramSpaceAttribute>* attributes) {
  return false;
}

bool FuzzedTpmManagerUtility::LockSpace(uint32_t index) {
  return false;
}

bool FuzzedTpmManagerUtility::GetOwnershipTakenSignalStatus(
    bool* is_successful,
    bool* has_received,
    tpm_manager::LocalData* local_data) {
  return false;
}

void FuzzedTpmManagerUtility::AddOwnershipCallback(
    OwnershipCallback ownership_callback) {}

void FuzzedTpmManagerUtility::OnOwnershipTaken(
    const tpm_manager::OwnershipTakenSignal& signal) {}

void FuzzedTpmManagerUtility::OnSignalConnected(
    const std::string& interface_name,
    const std::string& signal_name,
    bool is_successful) {}

}  // namespace chaps
