// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/network_context.h"

#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include <base/strings/strcat.h>
#include <base/strings/string_number_conversions.h>

namespace shill {
namespace {
constexpr char kNoService[] = "no_service";
}  // namespace

int NetworkContext::next_session_id_ = 1;

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

void NetworkContext::UpdateSessionId() {
  session_id_ = next_session_id_++;
  if (next_session_id_ == std::numeric_limits<int>::max()) {
    // Handle overflow properly.
    next_session_id_ = 1;
  }
  GenerateLoggingTag();
}

void NetworkContext::ClearSessionId() {
  session_id_ = std::nullopt;
  GenerateLoggingTag();
}

void NetworkContext::GenerateLoggingTag() {
  // Add "sid=" in logs to give more context for this number to the readers, and
  // also make it more searchable.
  logging_tag_ = base::StrCat(
      {ifname_, " ", service_logging_name_, " sid=",
       (session_id_ ? base::NumberToString(session_id_.value()) : "none")});
}

}  // namespace shill
