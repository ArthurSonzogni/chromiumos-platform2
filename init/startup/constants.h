// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_CONSTANTS_H_
#define INIT_STARTUP_CONSTANTS_H_

#include <sys/mount.h>

namespace startup {

// These constants are used to check the clock. Since they need to be
// updated, which could be automated, they are in a separate file for
// ease of maintenance.
// NB: Keep in sync with tlsdate/configure.ac constants.
constexpr int kMinYear = 2023;
// Assume that this build of the OS won't be used this far in the future.
// Seems unlikely that we'll be able to reasonably communicate with the
// rest of the world with such ancient software.  It doesn't prevent the
// runtime from syncing the clock.
constexpr int kMaxYear = kMinYear + 30;
// This isn't exactly correct as it doesn't handle leap years, but it's
// good enough for our purposes (pulling clock to the ~last year).
constexpr uint64_t kMinSecs = (kMinYear - 1970) * (365UL * 24 * 60 * 60);
constexpr uint64_t kMaxSecs = (kMaxYear - 1970) * (365UL * 24 * 60 * 60);

// Many of the mount calls in chromeos_startup utilize these flags.
// Making this a constant to simplify those mount calls, but this
// should only be used in cases where these specific mount flags are
// needed.
constexpr int kCommonMountFlags = MS_NOSUID | MS_NODEV | MS_NOEXEC;

// TPM Owned path, used to determine whether the TPM is owned.
constexpr char kTPMOwnedPath[] = "sys/class/tpm/tpm0/device/owned";

}  // namespace startup

#endif  // INIT_STARTUP_CONSTANTS_H_
