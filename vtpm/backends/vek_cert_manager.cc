// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vtpm/backends/vek_cert_manager.h"

#include <string>

#include <base/check.h>
#include <trunks/tpm_generated.h>

namespace vtpm {

VekCertManager::VekCertManager(trunks::TPM_NV_INDEX index, Blob* blob)
    : nv_index_(index), blob_(blob) {
  CHECK(blob_);
}

trunks::TPM_RC VekCertManager::Read(trunks::TPM_NV_INDEX nv_index,
                                    const std::string& password,
                                    std::string& nv_data) {
  if (nv_index != nv_index_) {
    return trunks::TPM_RC_NV_SPACE;
  }
  // Only accepts empty auth.
  if (!password.empty()) {
    return trunks::TPM_RC_BAD_AUTH;
  }
  return blob_->Get(nv_data);
}

}  // namespace vtpm
