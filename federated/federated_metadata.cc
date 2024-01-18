// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/federated_metadata.h"

#include <base/containers/fixed_flat_map.h>

#include "base/no_destructor.h"

namespace federated {
namespace {

using chromeos::federated::mojom::FederatedExampleTableId;

static constexpr auto kTableIdToName =
    base::MakeFixedFlatMap<FederatedExampleTableId, std::string_view>({
        {FederatedExampleTableId::TIMEZONE_CODE, "timezone_code_phh"},
        {FederatedExampleTableId::LAUNCHER_QUERY,
         "launcher_query_analytics_v1"},
        {FederatedExampleTableId::LAUNCHER_QUERY_V2,
         "launcher_query_analytics_v2"},
        {FederatedExampleTableId::INPUT_AUTOCORRECT, "input_autocorrect_phh"},
    });

}  // namespace

// Reads the values of `kTableIdToName` as a set.
std::unordered_set<std::string> GetRegisteredTableNames() {
  static const base::NoDestructor<std::unordered_set<std::string>> table_names(
      [] {
        std::unordered_set<std::string> set;
        for (const auto& [id, table_name] : kTableIdToName) {
          set.insert(std::string(table_name));
        }
        return set;
      }());

  return *table_names;
}

bool IsTableNameRegistered(const std::string& table_name) {
  return GetRegisteredTableNames().contains(table_name);
}

std::optional<std::string> GetTableNameString(
    const FederatedExampleTableId table_id) {
  if (!kTableIdToName.contains(table_id))
    return std::nullopt;
  return std::string(kTableIdToName.at(table_id));
}

}  // namespace federated
