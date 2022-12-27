// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/status/status.h"

#include <iostream>
#include <memory>
#include <stdlib.h>

#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/service_constants.h>

#include "diagnostics/cros_health_tool/mojo_util.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

// String printed when a value is true.
constexpr char kTrueString[] = "true";
// String printed when a value is false.
constexpr char kFalseString[] = "false";
}  // namespace

// 'status' sub-command for cros-health-tool:
//
// Utility that queries the status of the cros_healthd daemon and external mojo
// remotes.
int status_main(int argc, char** argv) {
  mojo::Remote<mojom::CrosHealthdSystemService> remote;
  RequestMojoServiceWithDisconnectHandler(
      chromeos::mojo_services::kCrosHealthdSystem, remote);

  MojoResponseWaiter<mojom::ServiceStatusPtr> waiter;
  remote->GetServiceStatus(waiter.CreateCallback());
  mojom::ServiceStatusPtr response = waiter.WaitForResponse();

  // If we got a response, the service is running.
  std::cout << "cros_health service status: running" << std::endl;
  std::cout << "network health mojo remote bound: ";
  std::cout << (response->network_health_bound ? kTrueString : kFalseString)
            << std::endl;
  std::cout << "network diagnostics mojo remote bound: ";
  std::cout << (response->network_diagnostics_bound ? kTrueString
                                                    : kFalseString)
            << std::endl;

  return EXIT_SUCCESS;
}

}  // namespace diagnostics
