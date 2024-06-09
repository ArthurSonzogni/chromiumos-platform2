// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_MOCK_MOUNT_VAR_HOME_H_
#define INIT_STARTUP_MOCK_MOUNT_VAR_HOME_H_

#include <base/files/file_path.h>
#include <base/values.h>
#include <gmock/gmock.h>
#include <libstorage/platform/platform.h>

#include "init/startup/mount_var_home_interface.h"
#include "init/startup/startup_dep_impl.h"

namespace startup {

// MountHelper contains the functionality for maintaining the mount stack
// and the mounting and umounting of /var and /home/chronos.
// This is the base class for the MountHelper classes. The pure virtual
// functions are defined within the StandardMountHelper, FactoryMountHelper,
// and StandardMountHelper classes.
class MockMountVarAndHomeChronos : public MountVarAndHomeChronosInterface {
 public:
  MockMountVarAndHomeChronos() {}
  virtual ~MockMountVarAndHomeChronos() = default;

  MOCK_METHOD(bool, Umount, (), (override));
  MOCK_METHOD(bool, Mount, (), (override));
};

}  // namespace startup

#endif  // INIT_STARTUP_MOCK_MOUNT_VAR_HOME_H_
