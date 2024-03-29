// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_NDT_CLIENT_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_NDT_CLIENT_H_

#include <memory>
#include <optional>
#include <string>

#include <mojo/public/cpp/bindings/pending_remote.h>

#include "diagnostics/cros_healthd/mojom/executor.mojom-forward.h"

namespace measurementlab::libndt7 {
class Client;
}  // namespace measurementlab::libndt7

// The NDT client version for healthd.
constexpr char kNdtClientVersion[] = "v0.1.0";

namespace diagnostics {

// Runs bandwidth test and returns the average speed in Kbps or null if the test
// is failed. This function should not be run in the main thread since it is a
// synchronous call.
std::optional<double> RunNdtTest(
    ash::cros_healthd::mojom::NetworkBandwidthTestType type,
    const std::string& oem_name,
    mojo::PendingRemote<ash::cros_healthd::mojom::NetworkBandwidthObserver>
        observer);

// Runs bandwidth test with NDT client. This function is exported for testing.
std::optional<double> RunNdtTestWithClient(
    ash::cros_healthd::mojom::NetworkBandwidthTestType type,
    std::unique_ptr<measurementlab::libndt7::Client> client);

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_NDT_CLIENT_H_
