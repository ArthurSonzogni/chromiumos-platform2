// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/fwmp_checker_platform_index.h"

#include <vector>

#include <tpm_manager/client/tpm_manager_utility.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>

#include <base/logging.h>

namespace cryptohome {

namespace {

bool HasAttribute(
    const std::vector<tpm_manager::NvramSpaceAttribute>& attributes,
    tpm_manager::NvramSpaceAttribute target) {
  return std::find(attributes.cbegin(), attributes.cend(), target) !=
         attributes.cend();
}

}  // namespace

FwmpCheckerPlatformIndex::FwmpCheckerPlatformIndex(
    tpm_manager::TpmManagerUtility* tpm_manager_utility)
    : tpm_manager_utility_(tpm_manager_utility) {}

bool FwmpCheckerPlatformIndex::IsValidForWrite(uint32_t nv_index) {
  if (!InitializeTpmManagerUtility()) {
    return false;
  }
  uint32_t size;
  bool is_read_locked;
  bool is_write_locked;
  std::vector<tpm_manager::NvramSpaceAttribute> attributes;
  if (!tpm_manager_utility_->GetSpaceInfo(nv_index, &size, &is_read_locked,
                                          &is_write_locked, &attributes)) {
    LOG(ERROR) << __func__ << ": Failed to call `GetSpaceinfo()`.";
    return false;
  }
  bool result = true;
  if (!HasAttribute(attributes, tpm_manager::NVRAM_PLATFORM_CREATE)) {
    LOG(ERROR) << __func__ << ": Not a platform-create index.";
    result = false;
  }
  if (!HasAttribute(attributes, tpm_manager::NVRAM_OWNER_WRITE)) {
    LOG(ERROR) << __func__ << ": Not a owner-write index.";
    result = false;
  }
  if (!HasAttribute(attributes, tpm_manager::NVRAM_READ_AUTHORIZATION)) {
    LOG(ERROR) << __func__ << ": Not a auth-read index.";
    result = false;
  }
  if (!HasAttribute(attributes, tpm_manager::NVRAM_PLATFORM_READ)) {
    LOG(ERROR) << __func__ << ": Not a platform-read index.";
    result = false;
  }
  // The attributes should be exact; however for future proof the check against
  // the additional attributes is still in a ad-hoc manner in case there is any
  // change to the attributes.
  if (HasAttribute(attributes, tpm_manager::NVRAM_WRITE_AUTHORIZATION)) {
    LOG(ERROR) << __func__ << ": Unexpected auth-write index.";
    result = false;
  }
  return result;
}

bool FwmpCheckerPlatformIndex::InitializeTpmManagerUtility() {
  if (!tpm_manager_utility_) {
    tpm_manager_utility_ = tpm_manager::TpmManagerUtility::GetSingleton();
    if (!tpm_manager_utility_) {
      LOG(ERROR) << __func__ << ": Failed to get TpmManagerUtility singleton!";
      return false;
    }
  }
  if (!tpm_manager_utility_->Initialize()) {
    LOG(ERROR) << __func__ << ": Failed to initialize tpm manager utility.";
    return false;
  }
  return true;
}

}  // namespace cryptohome
