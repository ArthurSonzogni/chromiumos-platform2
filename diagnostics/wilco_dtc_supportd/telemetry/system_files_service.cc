// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/telemetry/system_files_service.h"

namespace diagnostics {

SystemFilesService::FileDump::FileDump() = default;

SystemFilesService::FileDump::~FileDump() = default;

SystemFilesService::FileDump::FileDump(SystemFilesService::FileDump&& other) =
    default;

SystemFilesService::FileDump& SystemFilesService::FileDump::operator=(
    SystemFilesService::FileDump&& other) = default;

}  // namespace diagnostics
