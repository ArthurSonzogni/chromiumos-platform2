// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/federated_metadata.h"

#include <vector>

#include "base/no_destructor.h"

namespace federated {
namespace {

// TODO(alanlxl): just for testing.
const std::vector<ClientConfigMetadata> kClientMetaVector = {
    {
        "analytics_test_population",
        "/tmp/",
        "",
    },
};

}  // namespace

const std::unordered_map<std::string, ClientConfigMetadata> GetClientConfig() {
  static const base::NoDestructor<
      std::unordered_map<std::string, ClientConfigMetadata>>
      client_config_map([] {
        std::unordered_map<std::string, ClientConfigMetadata> map;
        for (const auto& meta : kClientMetaVector) {
          map[meta.name] = meta;
        }
        return map;
      }());

  return *client_config_map;
}

const std::unordered_set<std::string> GetClientNames() {
  static const base::NoDestructor<std::unordered_set<std::string>> client_names(
      [] {
        std::unordered_set<std::string> set;
        for (const auto& meta : kClientMetaVector) {
          set.insert(meta.name);
        }
        return set;
      }());

  return *client_names;
}

}  // namespace federated
