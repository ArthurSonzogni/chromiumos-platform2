// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <base/check.h>
#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "shill/vpn/openvpn_management_server.h"

namespace shill {

class OpenVPNManagementServerFuzzer {
 public:
  void Run(const uint8_t* data, size_t size) {
    // First just send random strings.
    FuzzedDataProvider provider(data, size);
    OpenVPNManagementServer::ParseSubstring(
        provider.ConsumeRandomLengthString(1024),
        provider.ConsumeRandomLengthString(1024),
        provider.ConsumeRandomLengthString(1024));

    // Next force some of the logic to actually run.
    OpenVPNManagementServer::ParseSubstring(
        provider.ConsumeRandomLengthString(1024),
        provider.ConsumeBytesAsString(1), provider.ConsumeBytesAsString(1));

    // Next the helpers.
    OpenVPNManagementServer::ParsePasswordTag(
        provider.ConsumeRandomLengthString(1024));
    OpenVPNManagementServer::ParsePasswordFailedReason(
        provider.ConsumeRandomLengthString(1024));
  }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Turn off logging.
  logging::SetMinLogLevel(logging::LOGGING_FATAL);

  OpenVPNManagementServerFuzzer fuzzer;
  fuzzer.Run(data, size);
  return 0;
}

}  // namespace shill
