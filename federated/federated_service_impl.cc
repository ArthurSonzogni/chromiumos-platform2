// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/federated_service_impl.h"

#include <utility>

#include "federated/federated_metadata.h"
#include "federated/mojom/example.mojom.h"
#include "federated/utils.h"

#include <base/check.h>

namespace federated {

FederatedServiceImpl::FederatedServiceImpl(
    mojo::ScopedMessagePipeHandle pipe,
    base::OnceClosure disconnect_handler,
    StorageManager* const storage_manager)
    : storage_manager_(storage_manager),
      registered_clients_(GetClientNames()),
      receiver_(
          this,
          mojo::InterfaceRequest<chromeos::federated::mojom::FederatedService>(
              std::move(pipe))) {
  receiver_.set_disconnect_handler(std::move(disconnect_handler));
}

void FederatedServiceImpl::Clone(
    mojo::PendingReceiver<chromeos::federated::mojom::FederatedService>
        receiver) {
  clone_receivers_.Add(this, std::move(receiver));
}

void FederatedServiceImpl::ReportExample(
    const std::string& client_name,
    const chromeos::federated::mojom::ExamplePtr example) {
  DCHECK_NE(storage_manager_, nullptr) << "storage_manager_ is not ready!";
  if (registered_clients_.find(client_name) == registered_clients_.end()) {
    VLOG(1) << "Unknown client_name: " << client_name;
    return;
  }

  if (!example || !example->features || !example->features->feature.size()) {
    VLOG(1) << "Invalid/empty example received from client " << client_name;
    return;
  }

  if (!storage_manager_->OnExampleReceived(
          client_name,
          ConvertToTensorFlowExampleProto(example).SerializeAsString())) {
    VLOG(1) << "Failed to insert the example from client " << client_name;
  }
}

}  // namespace federated
