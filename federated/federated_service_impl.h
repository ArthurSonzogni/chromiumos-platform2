// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_FEDERATED_SERVICE_IMPL_H_
#define FEDERATED_FEDERATED_SERVICE_IMPL_H_

#include <optional>
#include <string>
#include <vector>

#include <base/containers/flat_map.h>
#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "federated/mojom/federated_service.mojom.h"
#include "federated/scheduler.h"
#include "federated/storage_manager.h"

namespace federated {

class FederatedServiceImpl
    : public chromeos::federated::mojom::FederatedService {
 public:
  // Creates an instance bound to `pipe`. The specified `disconnect_handler`
  // will be invoked if the binding encounters a connection error or is closed.
  //
  // Ownership is not taken of `storage_manager` and `scheduler`, and they must
  // therefore outlive this instance.
  FederatedServiceImpl(mojo::ScopedMessagePipeHandle pipe,
                       base::OnceClosure disconnect_handler,
                       StorageManager* const storage_manager,
                       Scheduler* const scheduler);
  FederatedServiceImpl(const FederatedServiceImpl&) = delete;
  FederatedServiceImpl& operator=(const FederatedServiceImpl&) = delete;

 private:
  // chromeos::federated::mojom::FederatedService:
  void Clone(mojo::PendingReceiver<chromeos::federated::mojom::FederatedService>
                 receiver) override;
  void ReportExample(const std::string& table_name,
                     chromeos::federated::mojom::ExamplePtr example) override;
  void StartScheduling(
      const std::optional<base::flat_map<std::string, std::string>>&
          client_launch_stage) override;

  void ReportExampleToTable(
      ::chromeos::federated::mojom::FederatedExampleTableId table_id,
      ::chromeos::federated::mojom::ExamplePtr example) override;

  // Starts scheduling federated training tasks for the given `client_configs`.
  // Does nothing if the federated service already schedules tasks.
  void StartSchedulingWithConfig(
      std::vector<chromeos::federated::mojom::ClientScheduleConfigPtr>
          client_configs) override;

  // Not owned.
  StorageManager* const storage_manager_;
  Scheduler* const scheduler_;

  // Primordial receiver bootstrapped over D-Bus. Once opened, is never closed.
  mojo::Receiver<chromeos::federated::mojom::FederatedService> receiver_;

  // Additional receivers bound via `Clone`.
  mojo::ReceiverSet<chromeos::federated::mojom::FederatedService>
      clone_receivers_;
};

}  // namespace federated

#endif  // FEDERATED_FEDERATED_SERVICE_IMPL_H_
