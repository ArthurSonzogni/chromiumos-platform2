// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SWAP_MANAGEMENT_SWAP_MANAGEMENT_DBUS_ADAPTOR_H_
#define SWAP_MANAGEMENT_SWAP_MANAGEMENT_DBUS_ADAPTOR_H_

#include <memory>
#include <string>

#include <base/timer/timer.h>
#include <brillo/dbus/exported_object_manager.h>
#include <brillo/dbus/exported_property_set.h>
#include <brillo/dbus/dbus_method_response.h>

#include "swap_management/dbus_adaptors/org.chromium.SwapManagement.h"
#include "swap_management/swap_tool.h"

namespace swap_management {

class SwapManagementDBusAdaptor
    : public org::chromium::SwapManagementAdaptor,
      public org::chromium::SwapManagementInterface {
 public:
  explicit SwapManagementDBusAdaptor(
      scoped_refptr<dbus::Bus> bus,
      std::unique_ptr<base::OneShotTimer> shutdown_timer);
  ~SwapManagementDBusAdaptor();

  // Register the D-Bus object and interfaces.
  void RegisterAsync(
      brillo::dbus_utils::AsyncEventSequencer::CompletionAction cb);

  bool SwapStart(brillo::ErrorPtr* error) override;
  bool SwapStop(brillo::ErrorPtr* error) override;
  bool SwapRestart(brillo::ErrorPtr* error) override;
  bool MGLRUSetEnable(brillo::ErrorPtr* error,
                      bool enable,
                      bool* out_result) override;
  std::string SwapEnable(int32_t size, bool change_now) override;
  std::string SwapDisable(bool change_now) override;
  std::string SwapStatus() override;
  std::string SwapSetParameter(const std::string& parameter_name,
                               uint32_t parameter_value) override;
  std::string SwapZramEnableWriteback(uint32_t size_mb) override;
  std::string SwapZramMarkIdle(uint32_t age) override;
  std::string SwapZramSetWritebackLimit(uint32_t limit) override;
  std::string InitiateSwapZramWriteback(uint32_t mode) override;

 private:
  brillo::dbus_utils::DBusObject dbus_object_;

  std::unique_ptr<SwapTool> swap_tool_;

  std::unique_ptr<base::OneShotTimer> shutdown_timer_;

  void ResetShutdownTimer();
};

}  // namespace swap_management

#endif  // SWAP_MANAGEMENT_SWAP_MANAGEMENT_DBUS_ADAPTOR_H_
