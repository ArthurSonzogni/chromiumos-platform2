// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FOURCC_H_
#define LIBEC_FOURCC_H_

#include <string>

#include "brillo/brillo_export.h"

namespace ec {

/**
 * Convert a Four Character Code to a printable string.
 *
 * If the FourCC contains non-printable characters, return a hex
 * representation of the four bytes.
 *
 * @param a FourCC code.
 * @return A printable form of the FourCC, of length 3 to 10.
 */
BRILLO_EXPORT std::string FourCCToString(uint32_t a);

}  // namespace ec

#endif  // LIBEC_FOURCC_H_
