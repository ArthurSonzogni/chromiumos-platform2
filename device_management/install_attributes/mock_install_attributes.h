// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_MOCK_INSTALL_ATTRIBUTES_H_
#define DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_MOCK_INSTALL_ATTRIBUTES_H_

#include "device_management/install_attributes/install_attributes.h"

#include <memory>
#include <string>

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

namespace device_management {

class MockInstallAttributes : public InstallAttributes {
 public:
  MockInstallAttributes() = default;
  virtual ~MockInstallAttributes() = default;

  MOCK_METHOD(bool, Init, (), (override));
  MOCK_METHOD(Status, status, (), (const, override));
  MOCK_METHOD(bool,
              Get,
              (const std::string&, brillo::Blob*),
              (const, override));
  MOCK_METHOD(bool, Set, (const std::string&, const brillo::Blob&), (override));
  MOCK_METHOD(bool, Finalize, (), (override));
  MOCK_METHOD(int, Count, (), (const, override));
  MOCK_METHOD(bool, IsSecure, (), (override));
};

}  // namespace device_management

#endif  // DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_MOCK_INSTALL_ATTRIBUTES_H_
