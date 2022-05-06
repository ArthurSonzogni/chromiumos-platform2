// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VTPM_BACKENDS_VEK_CERT_MANAGER_H_
#define VTPM_BACKENDS_VEK_CERT_MANAGER_H_

#include "vtpm/backends/nv_space_manager.h"

#include <string>

#include <trunks/tpm_generated.h>

#include "vtpm/backends/blob.h"

namespace vtpm {

class VekCertManager : public NvSpaceManager {
 public:
  VekCertManager(trunks::TPM_NV_INDEX index, Blob* blob);
  virtual ~VekCertManager() = default;

  trunks::TPM_RC Read(trunks::TPM_NV_INDEX nv_index,
                      const std::string& password,
                      std::string& nv_data) override;

 private:
  const trunks::TPM_NV_INDEX nv_index_;
  Blob* const blob_;
};

}  // namespace vtpm

#endif  // VTPM_BACKENDS_VEK_CERT_MANAGER_H_
