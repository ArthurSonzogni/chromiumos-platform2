// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VTPM_BACKENDS_REAL_TPM_PROPERTY_MANAGER_H_
#define VTPM_BACKENDS_REAL_TPM_PROPERTY_MANAGER_H_

#include "vtpm/backends/tpm_property_manager.h"

#include <vector>

namespace vtpm {

class RealTpmPropertyManager : public TpmPropertyManager {
 public:
  ~RealTpmPropertyManager() override = default;
  void AddCommand(trunks::TPM_CC cc) override;
  const std::vector<trunks::TPM_CC>& GetCommandList() override;

 private:
  std::vector<trunks::TPM_CC> commands_;
  bool is_sorted_ = true;
};

}  // namespace vtpm

#endif  // VTPM_BACKENDS_REAL_TPM_PROPERTY_MANAGER_H_
