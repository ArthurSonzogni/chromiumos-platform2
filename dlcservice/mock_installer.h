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

  MOCK_METHOD(void,
              Install,
              (const InstallArgs& install_args,
               InstallSuccessCallback success_callback,
               InstallFailureCallback failure_callback),
              (override));
};

}  // namespace dlcservice

#endif  // DLCSERVICE_MOCK_INSTALLER_H_
