// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_IMPL_H_

#include <memory>

#include "diagnostics/cros_healthd/system/mojo_service.h"
#include "diagnostics/cros_healthd/utils/mojo_relay.h"
#include "diagnostics/mojom/external/cros_healthd_internal.mojom.h"

namespace diagnostics {

// The implementation of MojoService.
class MojoServiceImpl : public MojoService {
 public:
  MojoServiceImpl(const MojoServiceImpl&) = delete;
  MojoServiceImpl& operator=(const MojoServiceImpl&) = delete;
  ~MojoServiceImpl() override;

  // Creates an instance with all the services initialized.
  static std::unique_ptr<MojoServiceImpl> Create();

  // MojoService overrides.
  chromeos::cros_healthd::internal::mojom::ChromiumDataCollector*
  GetChromiumDataCollector() override;

  // Gets the mojo relay. TODO(b/230064284): Remove this after migrate to
  // service manager.
  MojoRelay<chromeos::cros_healthd::internal::mojom::ChromiumDataCollector>&
  chromium_data_collector_relay() {
    return chromium_data_collector_relay_;
  }

 protected:
  MojoServiceImpl();

 private:
  // Mojo remotes or adaptors to access mojo interfaces.
  MojoRelay<chromeos::cros_healthd::internal::mojom::ChromiumDataCollector>
      chromium_data_collector_relay_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_IMPL_H_
