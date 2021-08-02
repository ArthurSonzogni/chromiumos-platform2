// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_FEDERATED_SERVICE_IMPL_H_
#define FEDERATED_FEDERATED_SERVICE_IMPL_H_

#include <string>
#include <unordered_set>

#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "federated/mojom/federated_service.mojom.h"
#include "federated/storage_manager.h"

namespace federated {

class FederatedServiceImpl
    : public chromeos::federated::mojom::FederatedService {
 public:
  // Creates an instance bound to `pipe`. The specified `disconnect_handler`
  // will be invoked if the binding encounters a connection error or is closed.
  //
  // Ownership is not taken of `storage_manager` and it must therefore outlive
  // this instance.
  FederatedServiceImpl(mojo::ScopedMessagePipeHandle pipe,
                       base::OnceClosure disconnect_handler,
                       StorageManager* storage_manager);
  FederatedServiceImpl(const FederatedServiceImpl&) = delete;
  FederatedServiceImpl& operator=(const FederatedServiceImpl&) = delete;

 private:
  // chromeos::federated::mojom::FederatedService:
  void Clone(mojo::PendingReceiver<chromeos::federated::mojom::FederatedService>
                 receiver) override;
  void ReportExample(const std::string& client_name,
                     chromeos::federated::mojom::ExamplePtr example) override;

  StorageManager* const storage_manager_;

  // All known clients, examples reported by unregistered clients will be
  // ignored.
  const std::unordered_set<std::string> registered_clients_;

  // Primordial receiver bootstrapped over D-Bus. Once opened, is never closed.
  mojo::Receiver<chromeos::federated::mojom::FederatedService> receiver_;

  // Additional receivers bound via `Clone`.
  mojo::ReceiverSet<chromeos::federated::mojom::FederatedService>
      clone_receivers_;
};

}  // namespace federated

#endif  // FEDERATED_FEDERATED_SERVICE_IMPL_H_
