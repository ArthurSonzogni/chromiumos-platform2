// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_UPDATE_STATUS_UTILS_H_
#define UPDATE_ENGINE_UPDATE_STATUS_UTILS_H_

#include <string>

#include "update_engine/client_library/include/update_engine/update_status.h"

namespace chromeos_update_engine {

const char* UpdateStatusToString(const update_engine::UpdateStatus& status);

std::string UpdateEngineStatusToString(
    const update_engine::UpdateEngineStatus& status);

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_UPDATE_STATUS_UTILS_H_
