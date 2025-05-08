// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBBRILLO_BRILLO_HWID_HWID_UTILS_H_
#define LIBBRILLO_BRILLO_HWID_HWID_UTILS_H_

#include <optional>
#include <string>

#include <brillo/brillo_export.h>

namespace brillo::hwid {

// Implement base8192 decoding used by hwid.
// Returns a string of '0' and '1' suitable for decoding.
BRILLO_EXPORT std::optional<std::string> DecodeHWID(
    const std::string_view hwid);

// Converts the given binary string to a base8192-encoded string.
// This function performs the following steps:
// 1. Encodes the `binary_payload` (which should not contain a checksum and EOS)
//    into its base8192 representation.
// 2. Prepends `hwid_prefix` to the base8192 encoded string from step 1.
// 3. Calculates the checksum based on the combined string.
// 4. Appends the checksum from step 3, and returns the appended string.
BRILLO_EXPORT std::optional<std::string> EncodeHWID(
    const std::string_view hwid_prefix, const std::string_view binary_payload);

// Calculate checksum from the given HWID string without checksum. The
// original checksum should be stripped before passing the HWID string to the
// function.
BRILLO_EXPORT std::optional<std::string> CalculateChecksum(
    const std::string_view hwid);

}  // namespace brillo::hwid

#endif  // LIBBRILLO_BRILLO_HWID_HWID_UTILS_H_
