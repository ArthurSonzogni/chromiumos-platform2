// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/federated_metadata.h"

#include <array>

#include "base/no_destructor.h"

namespace federated {
namespace {

// TODO(alanlxl): just for testing.
constexpr std::array<const char* [3], 1> kClientMetadata = {{
    {
        "analytics_test_population",
        "/tmp/",
        "",
    },
}};

}  // namespace

std::unordered_map<std::string, ClientConfigMetadata> GetClientConfig() {
  static const base::NoDestructor<
      std::unordered_map<std::string, ClientConfigMetadata>>
      client_config_map([] {
        std::unordered_map<std::string, ClientConfigMetadata> map;
        for (const auto& data : kClientMetadata) {
          const ClientConfigMetadata meta{data[0], data[1], data[2]};
          map[meta.name] = meta;
        }
        return map;
      }());

  return *client_config_map;
}

std::unordered_set<std::string> GetClientNames() {
  static const base::NoDestructor<std::unordered_set<std::string>> client_names(
      [] {
        std::unordered_set<std::string> set;
        for (const auto& data : kClientMetadata) {
          set.insert(data[0]);
        }
        return set;
      }());

  return *client_names;
}

}  // namespace federated
