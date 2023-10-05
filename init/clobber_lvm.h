// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_CLOBBER_LVM_H_
#define INIT_CLOBBER_LVM_H_

#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <brillo/blkdev_utils/lvm.h>
#include <libdlcservice/utils_interface.h>

#include "init/clobber_wipe.h"

constexpr char kThinpool[] = "thinpool";
constexpr char kUnencrypted[] = "unencrypted";

// Class for LVM operation, when device mapper is used.
class ClobberLvm {
 public:
  ClobberLvm(ClobberWipe* clobber_wipe,
             std::unique_ptr<brillo::LogicalVolumeManager> lvm);
  virtual ~ClobberLvm() = default;

  // Creates the necessary LVM devices specifically for preserving logical
  // volumes option during clobber.
  virtual std::optional<base::FilePath> CreateLogicalVolumeStackForPreserved(
      const base::FilePath stateful_partition_device);

  // Creates the necessary LVM devices.
  virtual std::optional<base::FilePath> CreateLogicalVolumeStack(
      const base::FilePath stateful_partition_device);

  // Removes the necessary LVM devices.
  virtual void RemoveLogicalVolumeStack(
      const base::FilePath stateful_partition_device);

  // TODO(b/302427976): Add some kind of pairing/grouping to infos, so they can
  // be batched. This will come in handy for DLCs/logical volumes that want to
  // be atomically operated on.
  struct PreserveLogicalVolumesWipeInfo {
    std::string lv_name;
    bool preserve = false;
    bool zero = false;

    struct DigestInfo {
      int64_t bytes = 0;
      std::vector<uint8_t> digest;
    };
    std::optional<DigestInfo> digest_info;

    struct Hash {
      auto operator()(const PreserveLogicalVolumesWipeInfo& info) const {
        // Use the logical volume name for uniqueness.
        return std::hash<std::string>{}(info.lv_name);
      }
    };

    bool operator==(const PreserveLogicalVolumesWipeInfo& o) const {
      // Again, use the logical volume name for uniqueness.
      return this->lv_name == o.lv_name;
    }
  };
  using PreserveLogicalVolumesWipeInfos =
      std::unordered_set<PreserveLogicalVolumesWipeInfo,
                         PreserveLogicalVolumesWipeInfo::Hash>;

  // Safe wipe of logical volumes.
  // Returns false if there are any failures during the safe wiping
  // (zeroing/preserving/removing) of individual logical volumes.
  virtual bool PreserveLogicalVolumesWipe(
      const base::FilePath stateful_partition_device,
      const PreserveLogicalVolumesWipeInfos& infos);
  virtual bool ProcessInfo(const brillo::VolumeGroup& vg,
                           const PreserveLogicalVolumesWipeInfo& info,
                           std::unique_ptr<dlcservice::UtilsInterface> utils);

  // Returns the argument list for preserved wipe of LVM specific to DLCs.
  // Takes in as argument:
  //   - the path to the DLC powerwash safe file
  //   - the path to the DLC manifest root
  virtual PreserveLogicalVolumesWipeInfos DlcPreserveLogicalVolumesWipeArgs(
      const base::FilePath& ps_file_path,
      const base::FilePath& dlc_manifest_root_path,
      dlcservice::PartitionSlot active_slot,
      std::unique_ptr<dlcservice::UtilsInterface> utils);

  virtual bool CreateUnencryptedStatefulLV(const brillo::VolumeGroup& vg,
                                           const brillo::Thinpool& thinpool,
                                           uint64_t lv_size);

  // Returns the argument list for preserved wipe of LVM.
  virtual PreserveLogicalVolumesWipeInfos PreserveLogicalVolumesWipeArgs(
      const dlcservice::PartitionSlot slot);

  virtual std::optional<uint64_t> GetPartitionSize(
      const base::FilePath& base_device);

 protected:
  // These functions are marked protected so they can be overridden for tests.

  // Wrapper around ioctl(_, BLKGETSIZE64, _). From cryptohome::Platform.
  virtual uint64_t GetBlkSize(const base::FilePath& device_size);

  // Generates a random volume group name for the stateful partition.
  virtual std::string GenerateRandomVolumeGroupName();

 private:
  ClobberWipe* clobber_wipe_;
  std::unique_ptr<brillo::LogicalVolumeManager> lvm_;
};

#endif  // INIT_CLOBBER_LVM_H_
