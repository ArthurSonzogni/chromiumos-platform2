// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mojom/mojo_portal_ui_interaction_handler.h"

#include <utility>

#include "shill/mojom/portal.mojom.h"

namespace shill {

MojoPortalUIInteractionHandler::MojoPortalUIInteractionHandler(
    NetworkManager* network_manager)
    : network_manager_(network_manager) {}

MojoPortalUIInteractionHandler::~MojoPortalUIInteractionHandler() = default;

void MojoPortalUIInteractionHandler::AddReceiver(
    mojo::PendingReceiver<
        chromeos::connectivity::mojom::PortalUIInteractionHandler> receiver) {
  receivers_.Add(this, std::move(receiver));
}

void MojoPortalUIInteractionHandler::OnNotificationEvent(
    int32_t network_id, NotificationEvent event) {
  Network* network = network_manager_->GetNetwork(network_id);
  if (network) {
    network->OnNotificationEvent(event);
  }
}

void MojoPortalUIInteractionHandler::OnSigninPageShown(
    int32_t network_id, const net_base::HttpUrl& url) {
  Network* network = network_manager_->GetNetwork(network_id);
  if (network) {
    network->OnSigninPageShown(url);
  }
}

void MojoPortalUIInteractionHandler::OnSigninPageLoaded(
    int32_t network_id, int32_t chrome_net_error) {
  Network* network = network_manager_->GetNetwork(network_id);
  if (network) {
    network->OnSigninPageLoaded(chrome_net_error);
  }
}

void MojoPortalUIInteractionHandler::OnSigninPageClosed(int32_t network_id) {
  Network* network = network_manager_->GetNetwork(network_id);
  if (network) {
    network->OnSigninPageClosed();
  }
}

}  // namespace shill
