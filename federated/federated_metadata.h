// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_FEDERATED_METADATA_H_
#define FEDERATED_FEDERATED_METADATA_H_

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace federated {

// The client config.
// TODO(alanlxl): this now only contains the minimal info, and might need to be
// converted to a proto to be used in the real federated tasks.
struct ClientConfigMetadata {
  std::string name;
  std::string base_dir;
  std::string retry_token;
};

// Returns a map from client_name to ClientConfigMetadata.
const std::unordered_map<std::string, ClientConfigMetadata> GetClientConfig();

// Returns a set of all registered client names;
const std::unordered_set<std::string> GetClientNames();

}  // namespace federated

#endif  // FEDERATED_FEDERATED_METADATA_H_
