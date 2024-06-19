// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOJOM_MOJO_PORTAL_SERVICE_H_
#define SHILL_MOJOM_MOJO_PORTAL_SERVICE_H_

#include <memory>

#include <mojo/public/cpp/bindings/receiver_set.h>

#include "shill/mojom/mojo_portal_ui_interaction_handler.h"
#include "shill/mojom/portal.mojom.h"

namespace shill {

class MojoPortalService : public chromeos::connectivity::mojom::PortalService {
 public:
  explicit MojoPortalService(NetworkManager* network_manager);

  ~MojoPortalService() override;

 private:
  // Implements chromeos::connectivity::mojom::PortalService.
  void ConnectPortalUIInteractionHandler(
      ::mojo::PendingReceiver<
          chromeos::connectivity::mojom::PortalUIInteractionHandler> receiver)
      override;

  // The handler that all the UI interaction events will be delegated to.
  std::unique_ptr<MojoPortalUIInteractionHandler> handler_;
};

}  // namespace shill

#endif  // SHILL_MOJOM_MOJO_PORTAL_SERVICE_H_
