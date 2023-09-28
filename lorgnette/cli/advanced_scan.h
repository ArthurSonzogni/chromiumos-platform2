// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_CLI_ADVANCED_SCAN_H_
#define LORGNETTE_CLI_ADVANCED_SCAN_H_

#include <string>

#include <base/strings/string_split.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>

#include "lorgnette/dbus-proxies.h"

namespace lorgnette::cli {

bool DoAdvancedScan(org::chromium::lorgnette::ManagerProxy* manager,
                    const std::string& scanner_name,
                    const base::StringPairs& scan_options,
                    const std::string& mime_type,
                    const std::string& output_pattern);

}  // namespace lorgnette::cli

#endif  // LORGNETTE_CLI_ADVANCED_SCAN_H_
