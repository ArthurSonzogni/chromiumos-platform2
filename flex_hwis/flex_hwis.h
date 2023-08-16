// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLEX_HWIS_FLEX_HWIS_H_
#define FLEX_HWIS_FLEX_HWIS_H_

#include "flex_hwis/flex_hwis_check.h"
#include "flex_hwis/http_sender.h"
#include "flex_hwis/hwis_data.pb.h"

#include <base/files/file_path.h>
#include <metrics/metrics_library.h>

namespace flex_hwis {
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

// This class is responsible for evaluating management policies and device
// settings and then sending the data to a remote API.
class FlexHwisSender {
 public:
  // |base_path| is normally "/" but can be adjusted for testing.
  FlexHwisSender(const base::FilePath& base_path,
                 policy::PolicyProvider& provider,
                 HttpSender& sender);
  // Send the device hardware information if policy allows.
  Result MaybeSend(hwis_proto::Device& hardware_info,
                   MetricsLibraryInterface& metrics);

 private:
  // The base FilePath, adjustable for testing.
  base::FilePath base_path_;
  flex_hwis::FlexHwisCheck check_;
  HttpSender& sender_;
};

}  // namespace flex_hwis

#endif  // FLEX_HWIS_FLEX_HWIS_H_
