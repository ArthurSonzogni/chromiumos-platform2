// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_RUNTIME_HWID_UTILS_H_
#define CRASH_REPORTER_RUNTIME_HWID_UTILS_H_

#include <chromeos/hardware_verifier/runtime_hwid_utils/runtime_hwid_utils.h>

namespace crash_runtime_hwid_utils {

// Gets the singleton instance of hardware_verifier::RuntimeHWIDUtils that
// provides functionalities to access Runtime HWID.
hardware_verifier::RuntimeHWIDUtils* GetInstance();

// Replaces the singleton instance of hardware_verifier::RuntimeHWIDUtils for
// testing. It returns the old instance before replacing so that the caller can
// replace it back easily.
hardware_verifier::RuntimeHWIDUtils* ReplaceInstanceForTest(
    hardware_verifier::RuntimeHWIDUtils* instance);

}  // namespace crash_runtime_hwid_utils

#endif  // CRASH_REPORTER_RUNTIME_HWID_UTILS_H_
