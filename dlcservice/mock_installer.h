// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_MOCK_INSTALLER_H_
#define DLCSERVICE_MOCK_INSTALLER_H_

#include <gmock/gmock.h>

#include "dlcservice/installer.h"

namespace dlcservice {

class MockInstaller : public InstallerInterface {
 public:
  MockInstaller() = default;

  MockInstaller(const MockInstaller&) = delete;
  MockInstaller& operator=(const MockInstaller&) = delete;

  MOCK_METHOD(void, AddObserver, (Observer * observer), (override));
  MOCK_METHOD(void, RemoveObserver, (Observer * observer), (override));
  MOCK_METHOD(bool, Init, (), (override));
  MOCK_METHOD(void,
              Install,
              (const InstallArgs& install_args,
               InstallSuccessCallback success_callback,
               InstallFailureCallback failure_callback),
              (override));
  MOCK_METHOD(bool, IsReady, (), (override));
  MOCK_METHOD(void, OnReady, (OnReadyCallback), (override));
  MOCK_METHOD(void, StatusSync, (), (override));
};

}  // namespace dlcservice

#endif  // DLCSERVICE_MOCK_INSTALLER_H_
