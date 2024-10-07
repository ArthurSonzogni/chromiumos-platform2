// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network_context.h"

#include <string>
#include <string_view>

#include <base/strings/strcat.h>

namespace shill {
namespace {
constexpr char kNoService[] = "no_service";
}

NetworkContext::NetworkContext(std::string_view ifname) : ifname_(ifname) {
  ClearServiceLoggingName();
}

NetworkContext::~NetworkContext() = default;

void NetworkContext::SetServiceLoggingName(std::string_view name) {
  service_logging_name_ = name;
  GenerateLoggingTag();
}

void NetworkContext::ClearServiceLoggingName() {
  SetServiceLoggingName(kNoService);
}

void NetworkContext::GenerateLoggingTag() {
  logging_tag_ = base::StrCat({ifname_, " ", service_logging_name_});
}

}  // namespace shill
