// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mojom/mojo_portal_ui_interaction_handler.h"

#include <utility>

#include "shill/mojom/portal.mojom.h"
#include "shill/network/portal_notification_event.h"

namespace shill {

void MojoPortalUIInteractionHandler::AddReceiver(
    mojo::PendingReceiver<
        chromeos::connectivity::mojom::PortalUIInteractionHandler> receiver) {
  receivers_.Add(this, std::move(receiver));
}

void MojoPortalUIInteractionHandler::OnNotificationEvent(
    int32_t network_id, PortalNotificationEvent event) {}

void MojoPortalUIInteractionHandler::OnSigninPageShown(
    int32_t network_id, const net_base::HttpUrl& url) {}

void MojoPortalUIInteractionHandler::OnSigninPageLoaded(
    int32_t network_id, int32_t chrome_net_error) {}

void MojoPortalUIInteractionHandler::OnSigninPageClosed(int32_t network_id) {}

}  // namespace shill
