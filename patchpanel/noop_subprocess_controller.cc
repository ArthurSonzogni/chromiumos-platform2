// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/noop_subprocess_controller.h"

#include <base/functional/callback.h>

namespace patchpanel {

NoopSubprocessController::NoopSubprocessController() = default;

NoopSubprocessController::~NoopSubprocessController() = default;

void NoopSubprocessController::SendControlMessage(
    const ControlMessage& proto) const {}

void NoopSubprocessController::Listen() {}

void NoopSubprocessController::RegisterFeedbackMessageHandler(
    base::RepeatingCallback<void(const FeedbackMessage&)> handler) {}

}  // namespace patchpanel
