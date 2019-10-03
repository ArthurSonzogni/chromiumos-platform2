// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CROS_DISKS_FUSE_MOUNT_MANAGER_H_
#define CROS_DISKS_FUSE_MOUNT_MANAGER_H_

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest_prod.h>

#include "cros-disks/mount_manager.h"

namespace cros_disks {

class FUSEHelper;

// Implementation of MountManager for mounting arbitrary FUSE-based filesystems.
// It essentially does dispatching of mount requests to individual FUSE helpers.
class FUSEMountManager : public MountManager {
 public:
  // |mount_root| - where mount points go.
  // |working_dirs_root| - where temporary working directories go.
  FUSEMountManager(const std::string& mount_root,
                   const std::string& working_dirs_root,
                   Platform* platform,
                   Metrics* metrics,
                   brillo::ProcessReaper* process_reaper);
  ~FUSEMountManager() override;

  bool Initialize() override;

  // Whether we know about FUSE driver able to handle this source. Note that
  // source doesn't have to be an actual file or path, it could be anything
  // identifying FUSE module and what instance to mount.
  bool CanMount(const std::string& source) const override;

  // Returns the type of mount sources supported by the manager.
  MountSourceType GetMountSourceType() const override {
    // TODO(crbug.com/831491): Introduce generic "FUSE" storage.
    return MOUNT_SOURCE_NETWORK_STORAGE;
  }

 protected:
  // Mounts |source| to |mount_path| as |fuse_type| with |options|.
  // |fuse_type| can be used to specify the type of |source|.
  // If |fuse_type| is an empty string, the type is determined based on the
  // format of the |source|. The underlying mounter may decide to apply mount
  // options different than |options|. |applied_options| is used to return
  // the mount options applied by the mounter.
  MountErrorType DoMount(const std::string& source,
                         const std::string& fuse_type,
                         const std::vector<std::string>& options,
                         const std::string& mount_path,
                         MountOptions* applied_options) override;

  // Unmounts |path| with |options|. Returns true if |path| is unmounted
  // successfully.
  MountErrorType DoUnmount(const std::string& path,
                           const std::vector<std::string>& options) override;

  // Returns a suggested mount path for a source.
  std::string SuggestMountPath(const std::string& source) const override;

  void RegisterHelper(std::unique_ptr<FUSEHelper> helper);

 private:
  FRIEND_TEST(FUSEMountManagerTest, DoUnmount);
  FRIEND_TEST(FUSEMountManagerTest, DoUnmount_Busy);
  FRIEND_TEST(FUSEMountManagerTest, SuggestMountPath);
  friend class FUSEMountManagerTest;

  std::vector<std::unique_ptr<FUSEHelper>> helpers_;
  const std::string working_dirs_root_;

  DISALLOW_COPY_AND_ASSIGN(FUSEMountManager);
};

}  // namespace cros_disks

#endif  // CROS_DISKS_FUSE_MOUNT_MANAGER_H_
