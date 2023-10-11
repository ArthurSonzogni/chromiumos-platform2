// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_CLOBBER_CLOBBER_STATE_H_
#define INIT_CLOBBER_CLOBBER_STATE_H_

#include <sys/stat.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/time/time.h>
#include <brillo/blkdev_utils/lvm.h>
#include <brillo/process/process.h>
#include <libcrossystem/crossystem.h>
#include <libdlcservice/utils_interface.h>

#include "init/clobber/clobber_lvm.h"
#include "init/clobber/clobber_state_log.h"
#include "init/clobber/clobber_ui.h"
#include "init/clobber/clobber_wipe.h"

class ClobberState {
 public:
  struct Arguments {
    // Run in the context of a factory flow, do not reboot when done.
    bool factory_wipe = false;
    // Less thorough data destruction.
    bool fast_wipe = false;
    // Don't delete the non-active set of kernel/root partitions.
    bool keepimg = false;
    // Preserve some files.
    bool safe_wipe = false;
    // Preserve rollback data.
    bool rollback_wipe = false;
    // Preserve initial reason for triggering clobber, if available.
    // Assume that the reason string is already sanitized by session
    // manager (non-alphanumeric characters replaced with '_').
    std::string reason = "";
    // Run in the context of an RMA flow. Additionally save the RMA
    // state file.
    bool rma_wipe = false;
    // Preserve the flag file used to skip some OOBE screens during the Chromad
    // to cloud migration.
    bool ad_migration_wipe = false;
    // Preserve LVM stateful without wiping entire stateful partition.
    // (Only supported/enforced on LVM stateful devices)
    bool preserve_lvs = false;
  };

  struct DeviceWipeInfo {
    // Paths under /dev for the various devices to wipe.
    base::FilePath stateful_partition_device;
    // Devices using logical volumes on the stateful partition will use a
    // logical volume on top of the stateful partition device.
    base::FilePath stateful_filesystem_device;
    base::FilePath inactive_root_device;
    base::FilePath inactive_kernel_device;

    // Is the stateful device backed by an MTD flash device.
    bool is_mtd_flash = false;
    // The partition number for the currently booted kernel partition.
    int active_kernel_partition = -1;
  };

  // Extracts ClobberState's arguments from argv.
  static Arguments ParseArgv(int argc, char const* const argv[]);

  // Attempts to increment the contents of `path` by 1. If the contents cannot
  // be read, or if the contents are not an integer, writes '1' to the file.
  static bool IncrementFileCounter(const base::FilePath& path);

  // Attempts to write the last powerwash time to `path`.
  // The `time` is that when the device have powerwash completed.
  static bool WriteLastPowerwashTime(const base::FilePath& path,
                                     const base::Time& time);

  // Given a list of files to preserve (relative to `preserved_files_root`),
  // creates a tar file containing those files at `tar_file_path`.
  // The directory structure of the preserved files is preserved.
  static int PreserveFiles(const base::FilePath& preserved_files_root,
                           const std::vector<base::FilePath>& preserved_files,
                           const base::FilePath& tar_file_path);

  // Determine the devices to be wiped and their properties, and populate
  // `wipe_info_out` with the results. Returns true if successful.
  static bool GetDevicesToWipe(const base::FilePath& root_disk,
                               const base::FilePath& root_device,
                               const ClobberWipe::PartitionNumbers& partitions,
                               DeviceWipeInfo* wipe_info_out);

  // Removes well-known keys from the VPD.
  static void RemoveVpdKeys();

  // ClobberState object relies on sub-objects to run:
  // cros_system : to access/mock crossystem
  // ui : to present data on screen
  // clobber_wipe: low level partition wiping
  // clobber_lvm : to deal with Logical Volumes.
  ClobberState(const Arguments& args,
               std::unique_ptr<crossystem::Crossystem> cros_system,
               std::unique_ptr<ClobberUi> ui,
               std::unique_ptr<ClobberWipe> clobber_wipe,
               std::unique_ptr<ClobberLvm> clobber_lvm);

  // Run the clobber state routine.
  int Run();

  bool IsInDeveloperMode();

  bool MarkDeveloperMode();

  // Attempt to switch rotational drives and drives that support
  // secure_erase_file to a fast wipe by taking some (secure) shortcuts.
  void AttemptSwitchToFastWipe(bool is_rotational);

  // If the stateful filesystem is available and the disk is rotational, do some
  // best-effort content shredding. Since on a rotational disk the filesystem is
  // not mounted with "data=journal", writes really do overwrite the block
  // contents (unlike on an SSD).
  void ShredRotationalStatefulFiles();

  // Wipe key information from the stateful partition for supported devices.
  bool WipeKeyMaterial();

  // Forces a delay, writing progress to the TTY.  This is used to prevent
  // developer mode transitions from happening too quickly.
  void ForceDelay();

  // Returns vector of files to be preserved. All FilePaths are relative to
  // stateful_.
  std::vector<base::FilePath> GetPreservedFilesList();

  // Copies encrypted stateful files to the unencrypted preserve directory.
  void PreserveEncryptedFiles();

  void SetArgsForTest(const Arguments& args);
  Arguments GetArgsForTest();
  void SetStatefulForTest(const base::FilePath& stateful_path);
  void SetRootPathForTest(const base::FilePath& root_path);

 private:
  bool ClearBiometricSensorEntropy();

  // Makes a new filesystem on `stateful_filesystem_device`.
  int CreateStatefulFileSystem(const std::string& stateful_filesystem_device);

  void Reboot();

  // Helper to wrap calls removing logical volumes and device level wipes.
  void ResetStatefulPartition();

  Arguments args_;
  std::unique_ptr<crossystem::Crossystem> cros_system_;
  std::unique_ptr<ClobberUi> ui_;
  base::FilePath stateful_;
  base::FilePath root_path_;
  ClobberWipe::PartitionNumbers partitions_;
  base::FilePath root_disk_;
  DeviceWipeInfo wipe_info_;
  base::TimeTicks wipe_start_time_;

  std::unique_ptr<ClobberLvm> clobber_lvm_;
  std::unique_ptr<ClobberWipe> clobber_wipe_;

  // Must be last in member variable list.
  base::WeakPtrFactory<ClobberState> weak_ptr_factory_;
};

#endif  // INIT_CLOBBER_CLOBBER_STATE_H_
