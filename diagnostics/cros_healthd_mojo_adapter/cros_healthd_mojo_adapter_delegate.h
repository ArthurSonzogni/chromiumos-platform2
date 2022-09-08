// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_DELEGATE_H_
#define DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_DELEGATE_H_

#include <optional>

#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/mojom/public/cros_healthd.mojom.h"

namespace diagnostics {

// Responsible for bootstrapping a mojo connection to cros_healthd.
class CrosHealthdMojoAdapterDelegate {
 public:
  virtual ~CrosHealthdMojoAdapterDelegate() = default;

  // Bootstraps a mojo connection to cros_healthd, then returns one end of the
  // bound pipe.
  virtual std::optional<mojo::PendingRemote<
      chromeos::cros_healthd::mojom::CrosHealthdServiceFactory>>
  GetCrosHealthdServiceFactory() = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_DELEGATE_H_
