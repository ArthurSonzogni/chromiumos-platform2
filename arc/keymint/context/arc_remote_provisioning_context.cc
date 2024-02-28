// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymint/context/arc_remote_provisioning_context.h"

namespace arc::keymint::context {

namespace {}  // namespace

ArcRemoteProvisioningContext::ArcRemoteProvisioningContext(
    keymaster_security_level_t security_level)
    : PureSoftRemoteProvisioningContext(security_level) {}

ArcRemoteProvisioningContext::~ArcRemoteProvisioningContext() = default;
}  // namespace arc::keymint::context
