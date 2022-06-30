// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_ERROR_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_ERROR_UTILS_H_

#include <string>

#include <base/logging.h>

#include "diagnostics/mojom/public/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace mojom = chromeos::cros_healthd::mojom;

// This helper function takes an error type and string error message and returns
// a ProbeError. In addition, the error message is logged to LOG(ERROR).
inline mojom::ProbeErrorPtr CreateAndLogProbeError(mojom::ErrorType type,
                                                   const std::string& msg) {
  auto error = mojom::ProbeError::New(type, msg);
  LOG(ERROR) << msg;
  return error;
}

// Appends message to the error. This can be used to append information for the
// error returned by a function. This join the message by ": " so don't add
// period `.` at the end of the message.
inline mojom::ProbeErrorPtr WrapProbeError(mojom::ProbeErrorPtr err,
                                           const std::string& msg) {
  return mojom::ProbeError::New(err->type, msg + ": " + err->msg);
}

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_ERROR_UTILS_H_
