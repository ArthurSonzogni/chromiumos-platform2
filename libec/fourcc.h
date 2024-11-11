// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBEC_FOURCC_H_
#define LIBEC_FOURCC_H_

#include <string>

#include "brillo/brillo_export.h"

namespace ec {

BRILLO_EXPORT std::string FourCCToString(uint32_t a);

}  // namespace ec

#endif  // LIBEC_FOURCC_H_
