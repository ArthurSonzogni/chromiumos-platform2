// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_INSTALL_ATTRIBUTES_H_
#define CRYPTOHOME_MOCK_INSTALL_ATTRIBUTES_H_

#include "cryptohome/install_attributes_interface.h"

#include <device_management/proto_bindings/device_management_interface.pb.h>
#include <device_management-client/device_management/dbus-proxies.h>

#include <memory>
#include <string>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

namespace cryptohome {

class MockInstallAttributes : public InstallAttributesInterface {
 public:
  MockInstallAttributes();
  virtual ~MockInstallAttributes();

  MOCK_METHOD(bool, Init, (), (override));
  MOCK_METHOD(bool,
              Get,
              (const std::string&, brillo::Blob*),
              (const, override));
  MOCK_METHOD(bool, Set, (const std::string&, const brillo::Blob&), (override));
  MOCK_METHOD(bool, Finalize, (), (override));
  MOCK_METHOD(int, Count, (), (const, override));
  MOCK_METHOD(bool, IsSecure, (), (override));
  MOCK_METHOD(InstallAttributesInterface::Status, status, (), (override));
  MOCK_METHOD(void,
              SetDeviceManagementProxy,
              (std::unique_ptr<org::chromium::DeviceManagementProxy> proxy),
              (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_INSTALL_ATTRIBUTES_H_
