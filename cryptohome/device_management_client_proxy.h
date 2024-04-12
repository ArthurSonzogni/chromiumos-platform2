// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// DeviceManagementClientProxy - proxy class for communicating with
// device_management service.

#ifndef CRYPTOHOME_DEVICE_MANAGEMENT_CLIENT_PROXY_H_
#define CRYPTOHOME_DEVICE_MANAGEMENT_CLIENT_PROXY_H_

#include <memory>
#include <string>
#include <vector>

#include <base/memory/ref_counted.h>
#include <base/time/time.h>
#include <cryptohome/proto_bindings/UserDataAuth.pb.h>
#include <dbus/bus.h>
#include <device_management/proto_bindings/device_management_interface.pb.h>
#include <device_management-client/device_management/dbus-proxies.h>

namespace cryptohome {
enum class InstallAttributesStatus {
  kUnknown,       // Not initialized yet.
  kTpmNotOwned,   // TPM not owned yet.
  kFirstInstall,  // Allows writing.
  kValid,         // Validated successfully.
  kInvalid,       // Not valid, e.g. clobbered, absent.
  COUNT,          // This is unused, just for counting the number of elements.
                  // Note that COUNT should always be the last element.
};
class DeviceManagementClientProxy {
 public:
  virtual ~DeviceManagementClientProxy() = default;
  DeviceManagementClientProxy();
  explicit DeviceManagementClientProxy(scoped_refptr<dbus::Bus> bus);
  virtual bool IsEnterpriseOwned();
  virtual bool IsInstallAttributesReady();
  virtual bool InstallAttributesFinalize();
  virtual bool InstallAttributesSet(const std::string& name,
                                    const std::vector<uint8_t>& data);
  virtual bool IsInstallAttributesFirstInstall();

 private:
  // Proxy object to access device_management service.
  std::unique_ptr<org::chromium::DeviceManagementProxy>
      device_management_proxy_;
  const int64_t kDefaultTimeout = base::Minutes(5).InMilliseconds();
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_DEVICE_MANAGEMENT_CLIENT_PROXY_H_
