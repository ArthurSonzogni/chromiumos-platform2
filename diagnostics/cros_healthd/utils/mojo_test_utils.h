// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_TEST_UTILS_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_TEST_UTILS_H_

#include <gmock/gmock.h>

namespace diagnostics {

// Save the mojom argument by copying.
ACTION_TEMPLATE(SaveMojomArg,
                HAS_1_TEMPLATE_PARAMS(int, k),
                AND_1_VALUE_PARAMS(output)) {
  *output = std::get<k>(args)->Clone();
}

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_MOJO_TEST_UTILS_H_
