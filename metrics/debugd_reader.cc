// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "metrics/debugd_reader.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <base/logging.h>
#include <dbus/bus.h>

namespace chromeos_metrics {

DebugdReader::DebugdReader(dbus::Bus* bus, std::string log_name)
    : log_name_(std::move(log_name)) {
  debugd_proxy_.reset(new org::chromium::debugdProxy(bus));
}

DebugdReader::~DebugdReader() = default;

std::optional<std::string> DebugdReader::Read() {
  brillo::ErrorPtr error;
  std::string log;

  debugd_proxy_->GetLog(log_name_, &log, &error);
  if (error) {
    LOG(ERROR) << "DBUS call failed with: " << error->GetMessage();
    return std::nullopt;
  }
  if (log.empty()) {
    return std::nullopt;
  }

  return log;
}

}  // namespace chromeos_metrics
