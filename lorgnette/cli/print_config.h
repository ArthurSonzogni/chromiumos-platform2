// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_CLI_PRINT_CONFIG_H_
#define LORGNETTE_CLI_PRINT_CONFIG_H_

#include <ostream>

#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

namespace lorgnette::cli {

void PrintScannerConfig(const lorgnette::ScannerConfig& config,
                        bool show_inactive,
                        bool show_advanced,
                        std::ostream& out);

}  // namespace lorgnette::cli

#endif  // LORGNETTE_CLI_PRINT_CONFIG_H_
