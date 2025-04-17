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

}  // namespace brillo::hwid

#endif  // LIBBRILLO_BRILLO_HWID_HWID_UTILS_H_
