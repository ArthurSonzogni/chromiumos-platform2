// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_IMPL_H_

#include <memory>

#include <mojo/public/cpp/bindings/remote.h>
#include <mojo_service_manager/lib/mojom/service_manager.mojom.h>

#include "diagnostics/cros_healthd/fake/fake_service_manager.h"
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
  chromeos::mojo_service_manager::mojom::ServiceManager* GetServiceManager()
      override;
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

  // Getters for subclass to modify the value.
  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>&
  service_manager() {
    return service_manager_;
  }

 private:
  // Mojo remotes or adaptors to access mojo interfaces.
  mojo::Remote<chromeos::mojo_service_manager::mojom::ServiceManager>
      service_manager_;
  MojoRelay<chromeos::cros_healthd::internal::mojom::ChromiumDataCollector>
      chromium_data_collector_relay_;

  // The fake service manager before we can use the real implementation.
  // TODO(b/244407986): Remove this temporary dependency.
  FakeServiceManager fake_service_manager_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_IMPL_H_
