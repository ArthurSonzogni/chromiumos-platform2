// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_DBUS_ADAPTOR_H_
#define SWAP_MANAGEMENT_DBUS_ADAPTOR_H_

#include "swap_management/dbus_adaptors/org.chromium.SwapManagement.h"
#include "swap_management/swap_tool.h"

#include <memory>
#include <string>

#include <brillo/dbus/exported_object_manager.h>
#include <brillo/dbus/exported_property_set.h>
#include <brillo/dbus/dbus_method_response.h>

namespace swap_management {

class DBusAdaptor : public org::chromium::SwapManagementAdaptor,
                    public org::chromium::SwapManagementInterface {
 public:
  explicit DBusAdaptor(scoped_refptr<dbus::Bus> bus);

  // Register the D-Bus object and interfaces.
  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb);

  bool SwapStart(brillo::ErrorPtr* error) override;
  bool SwapStop(brillo::ErrorPtr* error) override;
  bool SwapRestart(brillo::ErrorPtr* error) override;
  bool SwapSetSize(brillo::ErrorPtr* error, int32_t size) override;
  bool SwapSetSwappiness(brillo::ErrorPtr* error, uint32_t swappiness) override;
  std::string SwapStatus() override;
  bool SwapZramEnableWriteback(brillo::ErrorPtr* error,
                               uint32_t size_mb) override;
  bool SwapZramMarkIdle(brillo::ErrorPtr* error, uint32_t age) override;
  bool SwapZramSetWritebackLimit(brillo::ErrorPtr* error,
                                 uint32_t limit) override;
  bool InitiateSwapZramWriteback(brillo::ErrorPtr* error,
                                 uint32_t mode) override;
  bool MGLRUSetEnable(brillo::ErrorPtr* error, uint8_t value) override;

 private:
  brillo::dbus_utils::DBusObject dbus_object_;

  std::unique_ptr<SwapTool> swap_tool_;
};

}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_DBUS_ADAPTOR_H_
