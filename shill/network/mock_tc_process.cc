// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_tc_process.h"

#include <base/functional/callback_helpers.h>

namespace shill {

MockTCProcess::MockTCProcess() : TCProcess(nullptr, {}, base::DoNothing()) {}

MockTCProcess::~MockTCProcess() = default;

MockTCProcessFactory::MockTCProcessFactory() = default;

MockTCProcessFactory::~MockTCProcessFactory() = default;

}  // namespace shill
