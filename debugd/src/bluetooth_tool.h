// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_BLUETOOTH_TOOL_H_
#define DEBUGD_SRC_BLUETOOTH_TOOL_H_

#include <memory>
#include <string>

#include <base/files/scoped_file.h>
#include <brillo/errors/error.h>
#include <dbus/bus.h>

#include "debugd/src/sandboxed_process.h"
#include "debugd/src/session_manager_observer_interface.h"

namespace debugd {

class BluetoothTool : public SessionManagerObserverInterface {
 public:
  explicit BluetoothTool(scoped_refptr<dbus::Bus> bus) : bus_(bus) {}
  BluetoothTool(const BluetoothTool&) = delete;
  BluetoothTool& operator=(const BluetoothTool&) = delete;

  ~BluetoothTool() override = default;

  bool StartBtsnoop();
  void StopBtsnoop();
  bool CopyBtsnoop(const base::ScopedFD& fd);

  bool IsBtsnoopRunning();

 protected:
  // Test needs to override this to get a mock sandbox process.
  virtual std::unique_ptr<SandboxedProcess> CreateSandboxedProcess();

 private:
  bool GetCurrentUserObfuscatedName(std::string* out_name);
  bool StartSandboxedBtsnoop(const std::string& obfuscated_name);

  // From SessionManagerObserverInterface.
  void OnSessionStarted() override;

  // From SessionManagerObserverInterface.
  void OnSessionStopped() override;

  std::unique_ptr<SandboxedProcess> btmon_;
  scoped_refptr<dbus::Bus> bus_;
};

}  // namespace debugd

#endif  // DEBUGD_SRC_BLUETOOTH_TOOL_H_
