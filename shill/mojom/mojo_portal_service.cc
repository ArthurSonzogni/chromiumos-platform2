// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mojom/mojo_portal_service.h"

#include <memory>
#include <utility>

#include "shill/mojom/portal.mojom.h"

namespace shill {

MojoPortalService::MojoPortalService()
    : handler_(std::make_unique<MojoPortalUIInteractionHandler>()) {}
MojoPortalService::~MojoPortalService() = default;

void MojoPortalService::ConnectPortalUIInteractionHandler(
    mojo::PendingReceiver<
        chromeos::connectivity::mojom::PortalUIInteractionHandler> receiver) {
  handler_->AddReceiver(std::move(receiver));
}

}  // namespace shill
