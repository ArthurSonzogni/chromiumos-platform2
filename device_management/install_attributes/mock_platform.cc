// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright 2013 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_management/install_attributes/mock_platform.h"

#include "device_management/install_attributes/fake_platform.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace device_management {

MockPlatform::MockPlatform()
    : mock_process_(new NiceMock<brillo::ProcessMock>()),
      fake_platform_(new FakePlatform()) {
  ON_CALL(*this, DeleteFile(_))
      .WillByDefault(Invoke(fake_platform_.get(), &FakePlatform::DeleteFile));
  ON_CALL(*this, DeletePathRecursively(_))
      .WillByDefault(
          Invoke(fake_platform_.get(), &FakePlatform::DeletePathRecursively));
  ON_CALL(*this, FileExists(_))
      .WillByDefault(Invoke(fake_platform_.get(), &FakePlatform::FileExists));
  ON_CALL(*this, SyncDirectory(_))
      .WillByDefault(
          Invoke(fake_platform_.get(), &FakePlatform::SyncDirectory));
  ON_CALL(*this, ReadFile(_, _))
      .WillByDefault(Invoke(fake_platform_.get(), &FakePlatform::ReadFile));
  ON_CALL(*this, WriteFileAtomic(_, _, _))
      .WillByDefault(
          Invoke(fake_platform_.get(), &FakePlatform::WriteFileAtomic));
  ON_CALL(*this, WriteFileAtomicDurable(_, _, _))
      .WillByDefault(
          Invoke(fake_platform_.get(), &FakePlatform::WriteFileAtomicDurable));
}

MockPlatform::~MockPlatform() {}

}  // namespace device_management
