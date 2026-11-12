// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_SETUP_ARC_SETUP_H_
#define ARC_SETUP_ARC_SETUP_H_

#include <map>
#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/component_export.h>

#include "arc/setup/android_sdk_version.h"
#include "arc/setup/arc_setup_metrics.h"
#include "arc/setup/arc_setup_util.h"
#include "arc/setup/config.h"

namespace base {

class FilePath;

}  // namespace base

namespace brillo {

class CrosConfigInterface;

}  // namespace brillo

namespace arc {

struct ArcPaths;
class ArcSetupMetrics;

static constexpr char kContainerConfigJson[] =
    "/usr/share/arc-setup/config.json";
static constexpr char kVmConfigJson[] = "/usr/share/arcvm/config.json";

// This MUST be in sync with 'enum BootType' in metrics.mojom.
// Note: This enum has to be in sync with Android's arc-boot-type-detector.
enum class ArcBootType {
  UNKNOWN = 0,
  // This is for the very first (opt-in) boot.
  FIRST_BOOT = 1,
  // This is for the first boot after Chrome OS update which also updates the
  // ARC++ image.
  FIRST_BOOT_AFTER_UPDATE = 2,
  // This is for regular boot.
  REGULAR_BOOT = 3,
};

// Enum that describes what action ArcSetup will perform.
enum class Mode {
  PREPARE_HOST_GENERATED_DIR,
  APPLY_PER_BOARD_CONFIG,  // for ARCVM
  PREPARE_ARCVM_DATA,      // for ARCVM
  REMOVE_DATA,
  REMOVE_STALE_DATA,
};

// A class that does the actual setup (and stop) operations.
class COMPONENT_EXPORT(LIBARC_SETUP) ArcSetup {
 public:
  ArcSetup(Mode mode, const base::FilePath& config_json);
  ArcSetup(Mode mode, const ArcVmDataType arcvm_data_type);
  ArcSetup(const ArcSetup&) = delete;
  ArcSetup& operator=(const ArcSetup&) = delete;

  ~ArcSetup();

  // Does the setup or stop operations depending on the environment variable.
  // The return value is void because the function aborts when one or more of
  // the operations fail.
  void Run();

  void set_arc_mounter_for_testing(std::unique_ptr<ArcMounter> mounter) {
    arc_mounter_ = std::move(mounter);
  }
  const ArcMounter* arc_mounter_for_testing() const {
    return arc_mounter_.get();
  }

 private:
  // Sets up android-data directory. It bind-mounts
  // /home/root/<hash>/android-data to |bind_target|.
  void SetUpAndroidData(const base::FilePath& bind_target);

  // Detects and applies per-board hardware configurations.
  void ApplyPerBoardConfigurationsInternal(
      const base::FilePath& oem_mount_directory);

  // Sets up a subset property file for camera.
  void SetUpCameraProperty(const base::FilePath& build_prop);

  // Removes ARC /data in LVM stateful partition.
  void RemoveDataInLvm();

  // Fills |out_boot_type| with the boot type.
  // If Android's packages.xml exists, fills |out_data_sdk_version| with the SDK
  // version for the internal storage found in the XML. If packages.xml doesn't
  // exist, fills it with AndroidSdkVersion::UNKNOWN.
  void GetBootTypeAndDataSdkVersion(
      const base::FilePath& android_data_directory,
      ArcBootType* out_boot_type,
      AndroidSdkVersion* out_data_sdk_version);

  // Returns the SDK version read from packages.xml in ARCVM /data by internally
  // calling GetBootTypeAndDataSdkVersion().
  AndroidSdkVersion GetArcVmDataSdkVersion();

  // Sends details about updated version to UMA.
  void SendUpgradeMetrics(AndroidSdkVersion data_sdk_version);

  // Deletes Android data if we are doing an unsupported upgrade.
  void DeleteAndroidDataOnUpgrade(AndroidSdkVersion data_sdk_version);

  // Deletes Android media provider data on upgrade. Currently this is only
  // implemented for upgrade from P (b/179233339).
  void DeleteAndroidMediaProviderDataOnUpgrade(
      AndroidSdkVersion data_sdk_version);

  // Converts |version_str| to the enum.
  AndroidSdkVersion SdkVersionFromString(const std::string& version_str);

  // Gets the image's SDK version. This function returns UNKNOWN when the system
  // image hasn't been mounted yet.
  AndroidSdkVersion GetSdkVersion();

  // Called when arc-prepare-host-generated-dir is executed.
  void OnPrepareHostGeneratedDir();

  // Called when arc-apply-per-board-config is executed.
  void OnApplyPerBoardConfig();

  // Called when arcvm-prepare-data is executed.
  void OnPrepareArcVmData();

  // Called when arc-remove-data is executed.
  void OnRemoveData();

  // Called when arc-remove-stale-data is executed.
  void OnRemoveStaleData();

  // Returns system build property.
  std::string GetSystemBuildPropertyOrDie(const std::string& name);

  const Mode mode_;
  const Config config_;
  const ArcVmDataType arcvm_data_type_;
  std::unique_ptr<ArcMounter> arc_mounter_;
  std::unique_ptr<ArcPaths> arc_paths_;
  std::unique_ptr<ArcSetupMetrics> arc_setup_metrics_;

  // Used to prevent multiple reading system properties file. Once this file is
  // read, it's content is stored here. If map is empty this indicates that
  // properties file was never read.
  std::map<std::string, std::string> system_properties_;
};

}  // namespace arc

#endif  // ARC_SETUP_ARC_SETUP_H_
