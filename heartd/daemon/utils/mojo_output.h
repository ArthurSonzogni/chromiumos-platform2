// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEARTD_DAEMON_UTILS_MOJO_OUTPUT_H_
#define HEARTD_DAEMON_UTILS_MOJO_OUTPUT_H_

#include <string>

#include "heartd/mojom/heartd.mojom.h"

namespace heartd {

std::string ToStr(ash::heartd::mojom::ServiceName name);
std::string ToStr(ash::heartd::mojom::ActionType action);

}  // namespace heartd

#endif  // HEARTD_DAEMON_UTILS_MOJO_OUTPUT_H_
