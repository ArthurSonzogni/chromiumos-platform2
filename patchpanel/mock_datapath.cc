// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/mock_datapath.h"

namespace patchpanel {

MockDatapath::MockDatapath(MinijailedProcessRunner* process_runner,
                           System* system)
    : Datapath(process_runner, nullptr, system) {}

MockDatapath::~MockDatapath() = default;

}  // namespace patchpanel
