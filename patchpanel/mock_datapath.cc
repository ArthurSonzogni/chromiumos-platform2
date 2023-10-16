// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_datapath.h"

namespace patchpanel {

MockDatapath::MockDatapath() : Datapath(nullptr, nullptr, nullptr) {}

MockDatapath::~MockDatapath() = default;

}  // namespace patchpanel
