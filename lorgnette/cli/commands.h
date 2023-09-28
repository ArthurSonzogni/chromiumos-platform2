// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_CLI_COMMANDS_H_
#define LORGNETTE_CLI_COMMANDS_H_

#include <string_view>

#include <base/containers/fixed_flat_map.h>

namespace lorgnette::cli {

enum class Command {
  kList,
  kScan,
  kCancelScan,
  kGetJsonCaps,
  kShowConfig,
  kDiscover,
  kAdvancedScan,
  kSetOptions,
};

constexpr auto kCommandMap = base::MakeFixedFlatMap<std::string_view, Command>({
    {"advanced_scan", Command::kAdvancedScan},
    {"cancel_scan", Command::kCancelScan},
    {"discover", Command::kDiscover},
    {"get_json_caps", Command::kGetJsonCaps},
    {"list", Command::kList},
    {"scan", Command::kScan},
    {"show_config", Command::kShowConfig},
    {"set_options", Command::kSetOptions},
});

}  // namespace lorgnette::cli

#endif  // LORGNETTE_CLI_COMMANDS_H_
