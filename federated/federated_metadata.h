// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_FEDERATED_METADATA_H_
#define FEDERATED_FEDERATED_METADATA_H_

#include <optional>
#include <string>
#include <unordered_set>

#include "federated/mojom/tables.mojom.h"

namespace federated {

// The client config. One client corresponds to a task group deployed on the
// server. Its `name` must be identical to the population_name of this task, and
// on ChromeOS platform one population can only has one task group.
struct ClientConfigMetadata {
  // Unique identifier of the client that contains only lowercase letters,
  // numbers and underscore. Must not be empty.
  std::string name;
  // Leaves this empty when initialization. Could be altered with server
  // response.
  std::string retry_token;
  // The launch stage used to compose a unique population name together with
  // `name`. A client's launch stage is provided via the `StartScheduling` mojo
  // call. If no valid launch stage, scheduler will skip this client.
  std::string launch_stage;

  // The example storage table name. The client's task reads examples from table
  // with this name in the database. It must be a registered table name, see
  // federated_service_impl.cc.
  std::string table_name;
};

std::unordered_set<std::string> GetRegisteredTableNames();

bool IsTableNameRegistered(const std::string& table_name);

// Tries to map table id to a registered table name, return nullopt if failed.
std::optional<std::string> GetTableNameString(
    const chromeos::federated::mojom::FederatedExampleTableId table_id);

}  // namespace federated

#endif  // FEDERATED_FEDERATED_METADATA_H_
