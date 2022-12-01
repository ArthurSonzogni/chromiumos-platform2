// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include "diagnostics/cros_healthd/executor/utils/process_control.h"

namespace diagnostics {

ProcessControl::ProcessControl(std::unique_ptr<brillo::Process> process)
    : process_(std::move(process)) {}

ProcessControl::~ProcessControl() = default;

}  // namespace diagnostics
