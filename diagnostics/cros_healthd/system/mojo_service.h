// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_H_

namespace chromeos::cros_healthd::internal::mojom {
class ChromiumDataCollector;
}

namespace diagnostics {

// Interface for accessing external mojo services.
// TODO(b/237239654): Move network mojo interface here and clean up the network
// adaptors.
class MojoService {
 public:
  virtual ~MojoService() = default;

  // Returns the mojo interface to ChromiumDataCollector.
  virtual chromeos::cros_healthd::internal::mojom::ChromiumDataCollector*
  GetChromiumDataCollector() = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_MOJO_SERVICE_H_
