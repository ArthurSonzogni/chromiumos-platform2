// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/status/status.h"

#include <iostream>
#include <memory>
#include <stdlib.h>

#include <base/task/single_thread_task_executor.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"
#include "diagnostics/mojom/public/cros_healthd.mojom.h"

namespace diagnostics {

namespace {
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
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);

  std::unique_ptr<CrosHealthdMojoAdapter> adapter =
      CrosHealthdMojoAdapter::Create();

  chromeos::cros_healthd::mojom::ServiceStatusPtr response =
      adapter->GetServiceStatus();

  if (!response) {
    std::cout << "cros_healthd service status: not available" << std::endl;
    return EXIT_FAILURE;
  }

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
