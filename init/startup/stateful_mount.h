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
  std::vector<std::string> GenerateExt4Features();

  StatefulMount(const Flags& flags,
                const base::FilePath& root,
                const base::FilePath& stateful,
                libstorage::Platform* platform,
                StartupDep* startup_dep,
                MountHelper* mount_helper);

  virtual ~StatefulMount() = default;

  std::optional<base::Value> GetImageVars(base::FilePath json_file,
                                          std::string key);

  base::FilePath GetStateDev();

  void ClobberStateful(const std::vector<std::string>& clobber_args,
                       const std::string& clobber_message);

  bool AttemptStatefulMigration();
  void MountStateful();
  // For testing purposes, allow injecting partition variables,
  // instead of gathering them from the local .json file.
  void MountStateful(const base::FilePath& root_dev,
                     const base::Value& image_vars);

  bool DevUpdateStatefulPartition(const std::string& args);
  void DevGatherLogs(const base::FilePath& base_dir);
  void DevMountPackages();

 private:
  void AppendQuotaFeaturesAndOptions(std::vector<std::string>* sb_options,
                                     std::vector<std::string>* sb_features);
  const Flags flags_;
  const base::FilePath root_;
  const base::FilePath stateful_;

  raw_ptr<libstorage::Platform> platform_;
  raw_ptr<StartupDep> startup_dep_;
  raw_ptr<MountHelper> mount_helper_;
  bootstat::BootStat bootstat_;

  base::FilePath root_device_;
  base::FilePath state_dev_;
  std::optional<brillo::VolumeGroup> volume_group_;
  std::unique_ptr<libstorage::StorageContainerFactory>
      storage_container_factory_;
};

}  // namespace startup

#endif  // INIT_STARTUP_STATEFUL_MOUNT_H_
