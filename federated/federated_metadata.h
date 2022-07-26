// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_FEDERATED_METADATA_H_
#define FEDERATED_FEDERATED_METADATA_H_

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace federated {

// The client config. One client corresponds to a task group deployed on the
// server. Its `name` must be identical to the population_name of this task, and
// on ChromeOS platform one population can only has one task group.
// TODO(alanlxl): this now only contains the minimal info, and might need to be
// converted to a proto to be used in the real federated tasks.
struct ClientConfigMetadata {
  std::string name;
  std::string base_dir;
  std::string retry_token;
};

// Returns a map from client_name to ClientConfigMetadata.
std::unordered_map<std::string, ClientConfigMetadata> GetClientConfig();

// Returns a set of all registered client names;
std::unordered_set<std::string> GetClientNames();

}  // namespace federated

#endif  // FEDERATED_FEDERATED_METADATA_H_
