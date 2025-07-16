// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HARDWARE_VERIFIER_RUNTIME_HWID_UTILS_H_
#define HARDWARE_VERIFIER_RUNTIME_HWID_UTILS_H_

namespace hardware_verifier {

// Deletes the `/var/cache/runtime_hwid` file if it is present on the device.
bool DeleteRuntimeHWIDFromDevice();

}  // namespace hardware_verifier

#endif  // HARDWARE_VERIFIER_RUNTIME_HWID_UTILS_H_
