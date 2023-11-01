// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_BASE_PATHS_H_
#define DIAGNOSTICS_BASE_PATHS_H_

#include "diagnostics/base/path_utils.h"

// VAR_ put the paths before the variable names so it is easier to read.
#define VAR_(path, var) inline constexpr auto var = path
#define PATH_(...) MakePathLiteral(__VA_ARGS__)

namespace diagnostics::paths {

// TODO(b/308731445): Use this file to define paths.

namespace cros_config {

VAR_(PATH_("run", "chromeos-config", "v1"), kRoot);
VAR_(PATH_("run", "chromeos-config", "test"), kTestRoot);

}  // namespace cros_config

}  // namespace diagnostics::paths

#undef PATH_
#undef VAR_

#endif  // DIAGNOSTICS_BASE_PATHS_H_
