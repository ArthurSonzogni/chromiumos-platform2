// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_HWIS_FLEX_HWIS_H_
#define FLEX_HWIS_FLEX_HWIS_H_

#include "flex_hwis/flex_hwis_check.h"
#include "flex_hwis/flex_hwis_mojo.h"

#include <base/files/file_path.h>
#include <metrics/metrics_library.h>

namespace flex_hwis {
namespace mojom = ::ash::cros_healthd::mojom;
enum class Result {
  // Hardware data sent successfully.
  Sent,
  // Hardware data not sent because data has already been sent recently.
  HasRunRecently,
  // Hardware data not sent because the device policy does not allow it.
  NotAuthorized,
  // Encountered an Error.
  Error
};

enum class Debug {
  // The user enters the "--debug" flag to output debug messages and hardware
  // data.
  Print,
  None,
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class [[nodiscard]] PermissionResult {
  // The device is managed and all required device management policies
  // are enabled.
  kPolicySuccess = 0,
  // The device is managed but not all the required device management
  // policies are enabled.
  kPolicyDenial = 1,
  // The device is not managed but the consent has been granted via the OOBE.
  kOptInSuccess = 2,
  // The device is not managed and the consent hasn't been granted via the OOBE.
  kOptInDenial = 3,
  // An error occurred while getting the policy value.
  kError = 4,

  kMax = kError,
};

// This class is responsible for collecting device hardware information,
// evaluating management policies and device settings,
// and then sending the data to a remote API.
class FlexHwisSender {
 public:
  // |base_path| is normally "/" but can be adjusted for testing.
  explicit FlexHwisSender(const base::FilePath& base_path,
                          policy::PolicyProvider& provider);
  // Collect and send the device hardware information.
  Result CollectAndSend(MetricsLibraryInterface& metrics, Debug debug);
  // This function is used by tests only to set the telemetry info.
  void SetTelemetryInfoForTesting(mojom::TelemetryInfoPtr info);

 private:
  // The base FilePath, adjustable for testing.
  base::FilePath base_path_;
  flex_hwis::FlexHwisCheck check_;
  flex_hwis::FlexHwisMojo mojo_;
};

}  // namespace flex_hwis

#endif  // FLEX_HWIS_FLEX_HWIS_H_
