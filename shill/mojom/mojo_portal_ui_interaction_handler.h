// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOJOM_MOJO_PORTAL_UI_INTERACTION_HANDLER_H_
#define SHILL_MOJOM_MOJO_PORTAL_UI_INTERACTION_HANDLER_H_

#include <chromeos/net-base/http_url.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "shill/mojom/portal.mojom.h"

namespace shill {

// Use type alias to simplify the Mojo enum.
using chromeos::connectivity::mojom::NotificationEvent;

class MojoPortalUIInteractionHandler
    : public chromeos::connectivity::mojom::PortalUIInteractionHandler {
 public:
  // Adds the receiver of the Mojo interface.
  void AddReceiver(
      mojo::PendingReceiver<
          chromeos::connectivity::mojom::PortalUIInteractionHandler> receiver);

 protected:
  // Implements chromeos::connectivity::mojom::PortalUIInteractionHandler.
  void OnNotificationEvent(int32_t network_id,
                           NotificationEvent event) override;
  void OnSigninPageShown(int32_t network_id,
                         const net_base::HttpUrl& url) override;
  void OnSigninPageLoaded(int32_t network_id,
                          int32_t chrome_net_error) override;
  void OnSigninPageClosed(int32_t network_id) override;

 private:
  mojo::ReceiverSet<chromeos::connectivity::mojom::PortalUIInteractionHandler>
      receivers_;
};

}  // namespace shill

#endif  // SHILL_MOJOM_MOJO_PORTAL_UI_INTERACTION_HANDLER_H_
