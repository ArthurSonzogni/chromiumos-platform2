// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_STATEFUL_MOUNT_H_
#define INIT_STARTUP_STATEFUL_MOUNT_H_

#include <memory>
#include <stack>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>
#include <metrics/bootstat.h>

#include "init/startup/flags.h"
#include "init/startup/mount_helper.h"
#include "init/startup/startup_dep_impl.h"

namespace startup {

// This is the class for stateful mount functionality. It contains
// the logic and functions used for mounting the stateful partition,
// as well as other functionality related to the stateful partition.
class StatefulMount {
 public:
  std::vector<std::string> GenerateExt4Features(const Flags* flags);

  StatefulMount(const base::FilePath& root,
                const base::FilePath& stateful,
                libstorage::Platform* platform,
                StartupDep* startup_dep);

  virtual ~StatefulMount() = default;

  base::FilePath GetStateDev();

  void ClobberStateful(const base::FilePath& stateful_device,
                       const std::vector<std::string>& clobber_args,
                       const std::string& clobber_message,
                       MountHelper* mount_helper);

  bool AttemptStatefulMigration(const base::FilePath& stateful_device);
  void MountStateful(const base::FilePath& root_dev,
                     const Flags* flags,
                     MountHelper* mount_helper,
                     const base::Value& image_vars,
                     std::optional<encryption::EncryptionKey> key);

  void DevPerformStatefulUpdate();
  bool DevUpdateStatefulPartition(const std::string& args,
                                  bool enable_stateful_security_hardening);
  void DevGatherLogs(const base::FilePath& base_dir);
  void SetUpDirectory(const base::FilePath& path);
  void DevMountDevImage(MountHelper* mount_helper);

  void DevMountPackages(MountHelper* mount_helper,
                        bool enable_stateful_security_hardening);
  void RemoveEmptyDirectory(std::vector<base::FilePath> preserved_paths,
                            base::FilePath directory);

 private:
  void AppendQuotaFeaturesAndOptions(const Flags* flags,
                                     std::vector<std::string>* sb_options,
                                     std::vector<std::string>* sb_features);
  const base::FilePath root_;
  const base::FilePath stateful_;

  raw_ptr<libstorage::Platform> platform_;
  raw_ptr<StartupDep> startup_dep_;
  bootstat::BootStat bootstat_;

  base::FilePath root_device_;
  base::FilePath state_dev_;
  std::optional<brillo::VolumeGroup> volume_group_;
};

}  // namespace startup

#endif  // INIT_STARTUP_STATEFUL_MOUNT_H_
