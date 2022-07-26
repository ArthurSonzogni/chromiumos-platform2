// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_BACKEND_TPM2_STATE_H_
#define LIBHWSEC_BACKEND_TPM2_STATE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "libhwsec/backend/backend.h"
#include "libhwsec/status.h"

namespace hwsec {

class BackendTpm2;

class StateTpm2 : public Backend::State,
                  public Backend::SubClassHelper<BackendTpm2> {
 public:
  using SubClassHelper::SubClassHelper;
  StatusOr<bool> IsEnabled() override;
  StatusOr<bool> IsReady() override;
  Status Prepare() override;
};

}  // namespace hwsec

#endif  // LIBHWSEC_BACKEND_TPM2_STATE_H_
