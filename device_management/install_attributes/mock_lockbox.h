// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_MOCK_LOCKBOX_H_
#define DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_MOCK_LOCKBOX_H_

#include "device_management/install_attributes/lockbox.h"

#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

namespace device_management {

class MockLockbox : public Lockbox {
 public:
  MockLockbox();
  virtual ~MockLockbox();
  MOCK_METHOD(bool, Reset, (LockboxError*), (override));
  MOCK_METHOD(bool, Store, (const brillo::Blob&, LockboxError*), (override));
};

}  // namespace device_management

#endif  // DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_MOCK_LOCKBOX_H_
