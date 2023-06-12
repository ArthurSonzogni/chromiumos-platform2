// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_MOCK_PLATFORM_H_
#define DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_MOCK_PLATFORM_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/time/time.h>
#include <base/unguessable_token.h>
#include <brillo/process/process_mock.h>
#include <brillo/secure_blob.h>
#include <gmock/gmock.h>

#include "device_management/install_attributes/fake_platform.h"
#include "device_management/install_attributes/platform.h"

namespace device_management {

class MockPlatform : public Platform {
 public:
  MockPlatform();
  virtual ~MockPlatform();

  MOCK_METHOD(bool, DeleteFile, (const base::FilePath&), (override));
  MOCK_METHOD(bool, DeletePathRecursively, (const base::FilePath&));
  MOCK_METHOD(bool, SyncDirectory, (const base::FilePath&), (override));
  MOCK_METHOD(bool, FileExists, (const base::FilePath&), (const, override));
  MOCK_METHOD(bool,
              ReadFile,
              (const base::FilePath&, brillo::Blob*),
              (override));
  MOCK_METHOD(bool,
              WriteFileAtomic,
              (const base::FilePath&, const brillo::Blob&, mode_t mode),
              (override));
  MOCK_METHOD(bool,
              WriteFileAtomicDurable,
              (const base::FilePath&, const brillo::Blob&, mode_t mode),
              (override));

  brillo::ProcessMock* mock_process() { return mock_process_.get(); }

  FakePlatform* GetFake() { return fake_platform_.get(); }

 private:
  std::unique_ptr<brillo::Process> MockCreateProcessInstance() {
    auto res = std::move(mock_process_);
    mock_process_ =
        std::make_unique<::testing::NiceMock<brillo::ProcessMock>>();
    return res;
  }

  std::unique_ptr<brillo::ProcessMock> mock_process_;
  std::unique_ptr<FakePlatform> fake_platform_;
};

}  // namespace device_management

#endif  // DEVICE_MANAGEMENT_INSTALL_ATTRIBUTES_MOCK_PLATFORM_H_
