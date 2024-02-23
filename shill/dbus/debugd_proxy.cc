// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/dbus/debugd_proxy.h"

#include <base/memory/weak_ptr.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/debugd/dbus-constants.h>
#include <dbus/bus.h>
#include <debugd/dbus-proxies.h>

#include "shill/logging.h"
#include "shill/scope_logger.h"

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kDBus;
}  // namespace Logging

DebugdProxy::DebugdProxy(const scoped_refptr<dbus::Bus>& bus)
    : proxy_(new org::chromium::debugdProxy(bus)) {}

void DebugdProxy::GenerateFirmwareDump(const debugd::FirmwareDumpType& type) {
  proxy_->GenerateFirmwareDumpAsync(
      static_cast<uint32_t>(type),
      /*success_callback=*/
      base::BindOnce(&DebugdProxy::OnFirmwareDumpGenerationResponse,
                     weak_factory_.GetWeakPtr(), type),
      /*error_callback=*/
      base::BindOnce(&DebugdProxy::OnFirmwareDumpGenerationError,
                     weak_factory_.GetWeakPtr(), type));
}

void DebugdProxy::OnFirmwareDumpGenerationResponse(
    const debugd::FirmwareDumpType& type, bool success) const {
  if (!success) {
    LOG(ERROR) << __func__ << ": Request for firmware dump (type: "
               << static_cast<uint32_t>(type)
               << ") generation was responded, but "
                  "the firmware/driver execution failed";
    return;
  }
  SLOG(2) << __func__ << ": Request for firmware dump (type: "
          << static_cast<uint32_t>(type) << ") generation was successful";
}

void DebugdProxy::OnFirmwareDumpGenerationError(
    const debugd::FirmwareDumpType& type, brillo::Error* error) const {
  LOG(ERROR) << __func__ << ": Failed to generate firmware dump for type "
             << static_cast<uint32_t>(type) << "(" << error->GetCode()
             << "): " << error->GetMessage();
}

}  // namespace shill
