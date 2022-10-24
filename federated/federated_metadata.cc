// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/federated_metadata.h"

#include <array>

#include "base/no_destructor.h"

namespace federated {
namespace {

constexpr std::array<const char* [3], 2> kClientMetadata = {{
    {
        /*name=*/"analytics_test_population",
        /*retry_token=*/"",
    },
    {
        "timezone_code_population",
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
          const ClientConfigMetadata meta{data[0], data[1]};
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
