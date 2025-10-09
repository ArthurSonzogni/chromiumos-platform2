// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_RUNTIME_HWID_UTILS_RUNTIME_HWID_UTILS_H_
#define HARDWARE_VERIFIER_RUNTIME_HWID_UTILS_RUNTIME_HWID_UTILS_H_

#include <optional>
#include <string>

#include <brillo/brillo_export.h>

namespace hardware_verifier {

class BRILLO_EXPORT RuntimeHWIDUtils {
 public:
  virtual ~RuntimeHWIDUtils() = default;

  // Deletes the `/var/cache/runtime_hwid` file if it is present on the device.
  virtual bool DeleteRuntimeHWIDFromDevice() const = 0;

  // Gets the Runtime HWID from `/var/cache/runtime_hwid`, verifies the content
  // and returns the Runtime HWID if the verification is successful.
  // Otherwise, returns the Factory HWID obtained from crossystem.
  virtual std::optional<std::string> GetRuntimeHWID() const = 0;
};

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_RUNTIME_HWID_UTILS_RUNTIME_HWID_UTILS_H_
