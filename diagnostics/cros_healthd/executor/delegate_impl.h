// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_DELEGATE_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_DELEGATE_IMPL_H_

#include "diagnostics/cros_healthd/executor/mojom/delegate.mojom.h"

namespace diagnostics {

namespace mojom = ::chromeos::cros_healthd::mojom;

class DelegateImpl : public mojom::Delegate {
 public:
  DelegateImpl();
  DelegateImpl(const DelegateImpl&) = delete;
  DelegateImpl& operator=(const DelegateImpl&) = delete;
  ~DelegateImpl() override;

  // chromeos::cros_healthd::mojom::Delegate overrides.
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_DELEGATE_IMPL_H_
