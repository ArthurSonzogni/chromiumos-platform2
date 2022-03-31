// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/backends/real_tpm_handle_manager.h"

#include <string>
#include <vector>

#include <base/check.h>
#include <base/logging.h>
#include <trunks/tpm_generated.h>

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
    std::map<trunks::TPM_HANDLE, Blob*> table)
    : handle_mapping_table_(table) {
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

}  // namespace vtpm
