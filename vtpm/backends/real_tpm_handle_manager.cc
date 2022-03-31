// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/backends/real_tpm_handle_manager.h"

#include <memory>
#include <string>
#include <vector>

#include <base/check.h>
#include <base/logging.h>
#include <trunks/tpm_generated.h>
#include <trunks/trunks_factory.h>

#include "vtpm/backends/scoped_host_key_handle.h"

namespace vtpm {

namespace {

// Defines the supported handle types file-statically so it can be called in
// the constructor.
bool DoesManagerSupportHandleType(trunks::TPM_HANDLE handle) {
  // Only persistent handle is supported for now.
  return (handle & trunks::HR_RANGE_MASK) == (trunks::HR_PERSISTENT);
}

}  // namespace

RealTpmHandleManager::RealTpmHandleManager(
    trunks::TrunksFactory* trunks_factory,
    std::map<trunks::TPM_HANDLE, Blob*> table)
    : trunks_factory_(trunks_factory), handle_mapping_table_(table) {
  CHECK(trunks_factory_);
  for (const auto& entry : handle_mapping_table_) {
    DCHECK(DoesManagerSupportHandleType(entry.first))
        << "Handle with Unsupported handle type: " << entry.first;
  }
}

bool RealTpmHandleManager::IsHandleTypeSuppoerted(trunks::TPM_HANDLE handle) {
  return DoesManagerSupportHandleType(handle);
}

trunks::TPM_RC RealTpmHandleManager::GetHandleList(
    trunks::TPM_HANDLE starting_handle,
    std::vector<trunks::TPM_HANDLE>* found_handles) {
  for (auto iter = handle_mapping_table_.lower_bound(starting_handle);
       iter != handle_mapping_table_.end(); ++iter) {
    Blob* blob = iter->second;
    std::string blob_not_used;
    const trunks::TPM_RC rc = blob->Get(blob_not_used);
    if (rc) {
      found_handles->clear();
      return rc;
    }
    // Note that the handle type is not validated because we support only 1 type
    // for now, and invalid entries are guarded in the constructor. But it wont
    // stand when we have multiple supported types that are maintained in
    // `handle_mapping_table_`.
    found_handles->push_back(iter->first);
  }
  return trunks::TPM_RC_SUCCESS;
}

trunks::TPM_RC RealTpmHandleManager::TranslateHandle(
    trunks::TPM_HANDLE handle, ScopedHostKeyHandle* host_handle) {
  // Currently this supports only known virtual "persistent key handles", while
  // the limitation is subject to change, for guest needs to load their key
  // blob(s).
  if (!IsHandleTypeSuppoerted(handle)) {
    return trunks::TPM_RC_HANDLE;
  }
  auto iter = handle_mapping_table_.find(handle);
  if (iter == handle_mapping_table_.end()) {
    return trunks::TPM_RC_HANDLE;
  }
  // Load the corresponding transient host key.
  std::string host_key_blob;
  trunks::TPM_RC rc = iter->second->Get(host_key_blob);
  if (rc) {
    return rc;
  }
  // Load the key to host TPM.
  // Always use the correct auth. If the guest feeds wrong auth, the follow-up
  // operation will fail anyway.
  std::unique_ptr<trunks::AuthorizationDelegate> empty_password_authorization =
      trunks_factory_->GetPasswordAuthorization(std::string());
  trunks::TPM_HANDLE raw_host_handle;
  rc = trunks_factory_->GetTpmUtility()->LoadKey(
      host_key_blob, empty_password_authorization.get(), &raw_host_handle);
  if (rc) {
    return rc;
  }

  // Construct the ScopedHostKeyHandle.
  *host_handle = ScopedHostKeyHandle(this, raw_host_handle, raw_host_handle);
  return trunks::TPM_RC_SUCCESS;
}

trunks::TPM_RC RealTpmHandleManager::FlushHostHandle(
    trunks::TPM_HANDLE handle) {
  return trunks_factory_->GetTpm()->FlushContextSync(
      handle, /*authorization_delegate=*/nullptr);
}

}  // namespace vtpm
