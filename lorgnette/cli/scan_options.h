// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_CLI_SCAN_OPTIONS_H_
#define LORGNETTE_CLI_SCAN_OPTIONS_H_

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/strings/string_split.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

namespace lorgnette::cli {

base::StringPairs GetScanOptions(const std::vector<std::string>& args);

std::optional<lorgnette::SetOptionsRequest> MakeSetOptionsRequest(
    const lorgnette::ScannerConfig& config, const base::StringPairs& options);

}  // namespace lorgnette::cli

#endif  // LORGNETTE_CLI_SCAN_OPTIONS_H_
