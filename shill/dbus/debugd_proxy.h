// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_DEBUGD_PROXY_H_
#define SHILL_DBUS_DEBUGD_PROXY_H_

#include <memory>

#include <base/memory/weak_ptr.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/debugd/dbus-constants.h>
#include <dbus/bus.h>
#include <debugd/dbus-proxies.h>

#include "shill/debugd_proxy_interface.h"

namespace shill {

class DebugdProxy : public DebugdProxyInterface {
 public:
  explicit DebugdProxy(const scoped_refptr<dbus::Bus>& bus);
  DebugdProxy(const DebugdProxy&) = delete;
  DebugdProxy& operator=(const DebugdProxy&) = delete;

  ~DebugdProxy() override = default;

  void GenerateFirmwareDump(const debugd::FirmwareDumpType& type) override;

 private:
  void OnFirmwareDumpGenerationResponse(const debugd::FirmwareDumpType& type,
                                        bool success) const;

  void OnFirmwareDumpGenerationError(const debugd::FirmwareDumpType& type,
                                     brillo::Error* error) const;

  std::unique_ptr<org::chromium::debugdProxyInterface> proxy_;

  base::WeakPtrFactory<DebugdProxy> weak_factory_{this};
};

}  // namespace shill

#endif  // SHILL_DBUS_DEBUGD_PROXY_H_
