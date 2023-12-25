// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_validation_log.h"

#include "shill/technology.h"

namespace shill {

MockValidationLog::MockValidationLog()
    : ValidationLog(Technology::kUnknown, nullptr) {}

MockValidationLog::~MockValidationLog() = default;

}  // namespace shill
