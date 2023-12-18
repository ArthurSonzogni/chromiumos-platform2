// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/system/fake_mojo_service.h"

namespace diagnostics {

FakeMojoService::FakeMojoService() = default;

FakeMojoService::~FakeMojoService() = default;

void FakeMojoService::InitializeFakeMojoService() {
  service_manager().Bind(
      fake_service_manager_.receiver().BindNewPipeAndPassRemote());

  chromium_data_collector().Bind(
      fake_chromium_data_collector_.receiver().BindNewPipeAndPassRemote());

  sensor_service().Bind(
      fake_sensor_service_.receiver().BindNewPipeAndPassRemote());

  network_health_service().Bind(
      fake_network_health_service_.receiver().BindNewPipeAndPassRemote());

  network_diagnostics_routines().Bind(
      fake_network_diagnostics_routines_.receiver().BindNewPipeAndPassRemote());
}

void FakeMojoService::ResetNetworkHealthService() {
  network_health_service().reset();
}

void FakeMojoService::ResetNetworkDiagnosticsRoutines() {
  network_diagnostics_routines().reset();
}

}  // namespace diagnostics
