// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SERVICE_CONFIG_H_
#define DIAGNOSTICS_CROS_HEALTHD_SERVICE_CONFIG_H_

namespace diagnostics {

// Carries options to change cros_healthd behaviors.
struct ServiceConfig {
  // If set, load chromeos-config from /run/chromeos-config/test
  bool test_cros_config = false;
  // If set, run the service with factory config.
  bool factory_mode = false;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SERVICE_CONFIG_H_
