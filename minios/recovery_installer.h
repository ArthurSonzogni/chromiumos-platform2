// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MINIOS_RECOVERY_INSTALLER_H_
#define MINIOS_RECOVERY_INSTALLER_H_

#include <memory>

#include "minios/process_manager.h"

namespace minios {

class RecoveryInstallerInterface {
 public:
  virtual ~RecoveryInstallerInterface() = default;

  virtual bool RepartitionDisk() = 0;
};

class RecoveryInstaller : public RecoveryInstallerInterface {
 public:
  explicit RecoveryInstaller(
      std::shared_ptr<ProcessManagerInterface> process_manager)
      : repartition_completed_(false), process_manager_(process_manager) {}
  ~RecoveryInstaller() override = default;

  RecoveryInstaller(const RecoveryInstaller&) = delete;
  RecoveryInstaller& operator=(const RecoveryInstaller&) = delete;

  bool RepartitionDisk() override;

 private:
  // Only repartition the disk once per recovery attempt.
  bool repartition_completed_;

  std::shared_ptr<ProcessManagerInterface> process_manager_;
};

}  // namespace minios

#endif  // MINIOS_RECOVERY_INSTALLER_H_
