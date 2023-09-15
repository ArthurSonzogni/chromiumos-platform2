// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_MOUNT_HELPER_FACTORY_H_
#define INIT_STARTUP_MOUNT_HELPER_FACTORY_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/values.h>
#include <libcrossystem/crossystem.h>

#include "init/startup/flags.h"
#include "init/startup/mount_helper.h"
#include "init/startup/platform_impl.h"

namespace startup {

// This class creates the MountHelper pointer that will be used in
// chromeos_startup. There is different functionality when the device
// is in factory mode or is a test image, so we use this class to determine
// which derived class to generate/utilize.
class MountHelperFactory {
 public:
  explicit MountHelperFactory(std::unique_ptr<Platform> platform,
                              const Flags& flags,
                              const base::FilePath& root,
                              const base::FilePath& stateful,
                              const base::FilePath& lsb_file);
  virtual ~MountHelperFactory() = default;

  // Generate the mount helper class to use by determining whether a device
  // is in dev mode, running a test image, and in factory mode. These different
  // possible device configurations need different implementations of the
  // functions DoMountVarAndHomeChronos and DoUmountVarAndHomeChronos.
  virtual std::unique_ptr<MountHelper> Generate(
      const crossystem::Crossystem& cros_system);

 private:
  std::unique_ptr<Platform> platform_;
  const Flags flags_;
  const base::FilePath root_;
  const base::FilePath stateful_;
  const base::FilePath lsb_file_;
};

}  // namespace startup

#endif  // INIT_STARTUP_MOUNT_HELPER_FACTORY_H_
