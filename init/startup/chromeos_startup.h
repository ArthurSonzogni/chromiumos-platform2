// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INIT_STARTUP_CHROMEOS_STARTUP_H_
#define INIT_STARTUP_CHROMEOS_STARTUP_H_

#include <memory>
#include <stack>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>
#include <libhwsec-foundation/tlcl_wrapper/tlcl_wrapper.h>
#include <libstorage/platform/platform.h>
#include <metrics/bootstat.h>
#include <vpd/vpd.h>

#include "init/metrics/metrics.h"
#include "init/startup/flags.h"
#include "init/startup/mount_helper.h"
#include "init/startup/mount_helper_factory.h"
#include "init/startup/startup_dep_impl.h"
#include "init/startup/stateful_mount.h"

namespace startup {

// This is the primary class for the startup functionality, making use of the
// other classes in the startup directory. chromeos_startup sets up different
// mount points, initializes kernel sysctl settings, configures security
// policies sets up the stateful partition, checks if we need a stateful wipe,
// gathers logs and collects crash reports.
class ChromeosStartup {
 public:
  // Process the included USE flags.
  static void ParseFlags(Flags* flags);
  // Process the included USE flags and command line arguments.
  static bool ParseFlags(Flags* flags, int argc, const char* argv[]);

  // Constructor for the class
  ChromeosStartup(std::unique_ptr<vpd::Vpd> vpd,
                  std::unique_ptr<Flags> flags,
                  const base::FilePath& root,
                  const base::FilePath& stateful,
                  const base::FilePath& metadata,
                  libstorage::Platform* platform,
                  StartupDep* startup_dep,
                  std::unique_ptr<MountHelperFactory> mount_helper_factory,
                  std::unique_ptr<libstorage::StorageContainerFactory>
                      storage_container_factory,
                  std::unique_ptr<hwsec_foundation::TlclWrapper> tlcl,
                  init_metrics::InitMetrics* metrics);

  virtual ~ChromeosStartup() = default;

  // Utility functions that are defined and run when in dev mode.
  // Returns if we are running on a debug build.
  bool DevIsDebugBuild() const;
  // Updated stateful partition if an update is pending.
  bool DevUpdateStatefulPartition(const std::string& args);
  // Gather logs.
  void DevGatherLogs();
  // Updated stateful partition if an update is pending.
  bool DevUpdateStatefulPartition();
  void DevMountPackages();
  // Restores the paths to preserve from protected path.
  void RestorePreservedPaths();

  std::optional<base::Value> GetImageVars(const base::FilePath& root,
                                          const base::FilePath& root_dev);

  // Returns if the TPM is owned or couldn't be determined.
  bool IsTPMOwned();
  // Load the TPM system key, based on data in the directory.
  std::optional<encryption::EncryptionKey> LoadTpmKey(
      const base::FilePath& tpm_data_dir, const base::FilePath& backup_file);

  // Returns if device needs to clobber even though there's no devmode file
  // present and boot is in verified mode.
  bool NeedsClobberWithoutDevModeFile();
  void Sysctl();
  void ForceCleanFileAttrs(const base::FilePath& path);
  bool IsVarFull();

  // EarlySetup contains the early mount calls of chromeos_startup. This
  // function exists to help break up the Run function into smaller functions.
  void EarlySetup();

  void TmpfilesConfiguration(const std::vector<std::string>& dirs);
  void CreateDaemonStore();
  void RemoveVarEmpty();
  void CheckVarLog();
  void RestoreContextsForVar(
      void (*restorecon_func)(libstorage::Platform* platform_,
                              const base::FilePath& path,
                              const std::vector<base::FilePath>& exclude,
                              bool is_recursive,
                              bool set_digests));

  // Possibly extend the PCR for ChromeOS version attestation.
  bool ExtendPCRForVersionAttestation();

  // Run the chromeos startup routine.
  int Run();

 protected:
  // Check whether the device is allowed to boot in dev mode.
  void DevCheckBlockDevMode(const base::FilePath& dev_mode_file) const;

  // Set dev_mode_ for tests.
  void SetDevMode(bool dev_mode);

  // Clean up after a TPM firmware update.
  void CleanupTpm(const base::FilePath& tpm_data_dir);

  // Move from /var/lib/whitelist to /var/lib/devicesettings.
  void MoveToLibDeviceSettings();

  // Set state_dev_ for tests.
  void SetStateDev(const base::FilePath& state_dev);

  // Set stateful_mount_ for tests.
  void SetStatefulMount(std::unique_ptr<StatefulMount> stateful_mount);

  // Set mount_helper_ for tests.
  void SetMountHelper(std::unique_ptr<MountHelper> mount_helper);

 private:
  friend class DevCheckBlockTest;
  friend class StatefulWipeTest;
  FRIEND_TEST(DevCheckBlockTest, DevSWBoot);
  FRIEND_TEST(DevCheckBlockTest, VpdCrosSysBlockDev);
  FRIEND_TEST(DevCheckBlockTest, CrosSysBlockDev);
  FRIEND_TEST(StatefulWipeTest, PowerwashForced);
  FRIEND_TEST(StatefulWipeTest, PowerwashNormal);
  FRIEND_TEST(StatefulWipeTest, NoStateDev);
  FRIEND_TEST(StatefulWipeTest, TransitionToVerifiedDevModeFile);
  FRIEND_TEST(StatefulWipeTest, TransitionToDevModeNoDebugBuild);
  FRIEND_TEST(StatefulWipeTestDevMode, TransitionToVerifiedDebugBuild);
  FRIEND_TEST(StatefulWipeTestDevMode, TransitionToDevModeDebugBuild);

  friend class TpmCleanupTest;
  FRIEND_TEST(TpmCleanupTest, TpmCleanupNoFlagFile);
  FRIEND_TEST(TpmCleanupTest, TpmCleanupNoCmdPath);
  FRIEND_TEST(TpmCleanupTest, TpmCleanupSuccess);

  friend class DeviceSettingsTest;
  FRIEND_TEST(DeviceSettingsTest, OldPathEmpty);
  FRIEND_TEST(DeviceSettingsTest, NewPathEmpty);
  FRIEND_TEST(DeviceSettingsTest, NeitherPathEmpty);

  friend class RestorePreservedPathsTest;
  FRIEND_TEST(RestorePreservedPathsTest, PopPaths);

  void CheckClock();
  // Returns if the device is transitioning between verified boot and
  // dev mode.
  bool IsDevToVerifiedModeTransition(int devsw_boot);

  // Check for whether we need a stateful wipe, and alert the use as
  // necessary.
  void CheckForStatefulWipe();

  // Mount /home.
  void MountHome();

  // Start tpm2-simulator if it exists.
  void StartTpm2Simulator();

  // Create directories inside run_ds based on etc_ds directory structure.
  void CreateDaemonStore(base::FilePath run_ds, base::FilePath etc_ds);

  raw_ptr<libstorage::Platform> platform_;
  std::unique_ptr<vpd::Vpd> vpd_;
  std::unique_ptr<Flags> flags_;
  const base::FilePath root_;
  base::FilePath root_dev_;
  const base::FilePath stateful_;
  const base::FilePath metadata_;
  bootstat::BootStat bootstat_;
  raw_ptr<StartupDep> startup_dep_;
  std::unique_ptr<MountHelper> mount_helper_;
  std::unique_ptr<MountHelperFactory> mount_helper_factory_;
  // storage_container_factory_ has short lifespan, from constructor to
  // start only, where is it passed to the mount_helper_ object.
  std::unique_ptr<libstorage::StorageContainerFactory>
      storage_container_factory_;
  bool enable_stateful_security_hardening_;
  std::unique_ptr<StatefulMount> stateful_mount_;
  bool dev_mode_;
  base::FilePath state_dev_;
  std::unique_ptr<hwsec_foundation::TlclWrapper> tlcl_;
  init_metrics::InitMetrics* metrics_;
};

}  // namespace startup

#endif  // INIT_STARTUP_CHROMEOS_STARTUP_H_
