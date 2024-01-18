// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/federated_service_impl.h"

#include <utility>

#include "federated/federated_metadata.h"
#include "federated/mojom/example.mojom.h"
#include "federated/utils.h"

#include <base/check.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>

namespace federated {

FederatedServiceImpl::FederatedServiceImpl(
    mojo::ScopedMessagePipeHandle pipe,
    base::OnceClosure disconnect_handler,
    StorageManager* const storage_manager,
    Scheduler* const scheduler)
    : storage_manager_(storage_manager),
      scheduler_(scheduler),
      receiver_(
          this,
          mojo::PendingReceiver<chromeos::federated::mojom::FederatedService>(
              std::move(pipe))) {
  receiver_.set_disconnect_handler(std::move(disconnect_handler));
}

void FederatedServiceImpl::Clone(
    mojo::PendingReceiver<chromeos::federated::mojom::FederatedService>
        receiver) {
  clone_receivers_.Add(this, std::move(receiver));
}

void FederatedServiceImpl::ReportExample(
    const std::string& table_name,
    const chromeos::federated::mojom::ExamplePtr example) {
  DCHECK_NE(storage_manager_, nullptr) << "storage_manager_ is not ready!";
  if (!IsTableNameRegistered(table_name)) {
    VLOG(1) << "Unknown table_name: " << table_name;
    return;
  }

  if (!example || !example->features || !example->features->feature.size()) {
    VLOG(1) << "Invalid/empty example received for table " << table_name;
    return;
  }

  if (!storage_manager_->OnExampleReceived(
          table_name,
          ConvertToTensorFlowExampleProto(example).SerializeAsString())) {
    VLOG(1) << "Failed to insert the example to table " << table_name;
  }
}

void FederatedServiceImpl::StartScheduling(
    const std::optional<base::flat_map<std::string, std::string>>&
        client_launch_stage) {
  // This is no-op if the scheduling already started.
  DVLOG(1) << "Received StartScheduling call.";
  scheduler_->Schedule(client_launch_stage);
}

void FederatedServiceImpl::ReportExampleToTable(
    chromeos::federated::mojom::FederatedExampleTableId table_id,
    chromeos::federated::mojom::ExamplePtr example) {
  const auto maybe_table_name = GetTableNameString(table_id);
  if (!maybe_table_name) {
    DVLOG(1) << "Unable to find the table name";
    return;
  }

  ReportExample(maybe_table_name.value(), std::move(example));
}

void FederatedServiceImpl::StartSchedulingWithConfig(
    std::vector<chromeos::federated::mojom::ClientScheduleConfigPtr>
        client_configs) {
  // This is no-op if the scheduling already started.
  DVLOG(1) << "Received StartScheduling call.";
  scheduler_->Schedule(client_configs);
}

}  // namespace federated
