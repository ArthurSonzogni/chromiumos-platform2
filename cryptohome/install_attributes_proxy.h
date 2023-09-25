// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// InstallAttributesProxy - forwards install_attributes requests to
// device_management service

#ifndef CRYPTOHOME_INSTALL_ATTRIBUTES_PROXY_H_
#define CRYPTOHOME_INSTALL_ATTRIBUTES_PROXY_H_

#include "cryptohome/install_attributes_interface.h"

#include <memory>
#include <string>
#include <utility>

#include <base/time/time.h>

namespace cryptohome {

class InstallAttributesProxy : public InstallAttributesInterface {
 public:
  InstallAttributesProxy() = default;
  InstallAttributesProxy(const InstallAttributesProxy&) = delete;
  InstallAttributesProxy& operator=(const InstallAttributesProxy&) = delete;

  ~InstallAttributesProxy() = default;

  // For the proxy class, this will just return true, as the actual
  // initialization will take place in device_management service.
  [[nodiscard]] bool Init() override { return true; };

  [[nodiscard]] bool Get(const std::string& name,
                         brillo::Blob* value) const override;

  [[nodiscard]] bool Set(const std::string& name,
                         const brillo::Blob& value) override;

  [[nodiscard]] bool Finalize() override;

  int Count() const override;

  bool IsSecure() override;

  Status status() override;

  void SetDeviceManagementProxy(
      std::unique_ptr<org::chromium::DeviceManagementProxy> proxy) override;

 private:
  // Proxy object to access device_management service.
  std::unique_ptr<org::chromium::DeviceManagementProxy>
      device_management_proxy_;
  const int64_t kDefaultTimeout = base::Minutes(5).InMilliseconds();
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_INSTALL_ATTRIBUTES_PROXY_H_
