// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_MANAGEMENT_CLIENT_CLIENT_H_
#define DEVICE_MANAGEMENT_CLIENT_CLIENT_H_

#include "device_management/client/printer.h"
#include "device_management-client/device_management/dbus-proxies.h"

#include <memory>
#include <string>

#include <base/command_line.h>
#include <base/memory/ref_counted.h>
#include <brillo/brillo_export.h>
#include <dbus/bus.h>

namespace device_management {

// Five minutes is enough to wait for any TPM operations, sync() calls, etc.
constexpr int kDefaultTimeoutMs = 5 * 60 * 1000;

// A class that manages communication with DeviceManagement.
class DeviceManagementClient {
 public:
  DeviceManagementClient(const DeviceManagementClient&) = delete;
  DeviceManagementClient& operator=(const DeviceManagementClient&) = delete;
  virtual ~DeviceManagementClient();

  // Creates DeviceManagementClient.
  static std::unique_ptr<DeviceManagementClient> CreateDeviceManagementClient();

  bool InitializePrinter(const base::CommandLine* cl);

  virtual bool IsInstallAttributesReady();

  virtual bool GetInstallAttributes(const base::CommandLine* cl);

  virtual bool SetInstallAttributes(const base::CommandLine* cl);

  virtual bool FinalizeInstallAttributes();

  virtual bool GetStatusInstallAttributes();

  virtual bool GetCountInstallAttributes();

  virtual bool IsReadyInstallAttributes();

  virtual bool IsSecureInstallAttributes();

  virtual bool IsInvalidInstallAttributes();

  virtual bool IsFirstInstallInstallAttributes();

  virtual bool GetFWMP();

  virtual bool SetFWMP(const base::CommandLine* cl);

  virtual bool RemoveFWMP();

 private:
  DeviceManagementClient(
      std::unique_ptr<org::chromium::DeviceManagementProxy> device_management,
      scoped_refptr<dbus::Bus> bus);
  std::unique_ptr<org::chromium::DeviceManagementProxy>
      device_management_proxy_;
  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<Printer> printer_;
  const int timeout_ms = kDefaultTimeoutMs;
};
}  // namespace device_management

#endif  // DEVICE_MANAGEMENT_CLIENT_CLIENT_H_
