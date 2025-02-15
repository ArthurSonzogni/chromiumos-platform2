// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "init/libpreservation/preservation.h"

#include <set>
#include <string>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

namespace libpreservation {
namespace {

std::set<std::string> GetSafeModePaths() {
  return std::set<std::string>({
      // Powerwash count path
      "unencrypted/preserve/powerwash_count",
      // TPM firmware update request.
      "unencrypted/preserve/tpm_firmware_update_request",
      // Rollback paths: Contains a boolean value indicating whether a rollback
      // has happened since the last update check where device policy was
      // available. Needed to avoid forced updates after rollbacks (device
      // policy is not yet loaded at this time).
      // Keep file names in sync with update_engine prefs.
      "unencrypted/preserve/update_engine/prefs/rollback-happened",
      "unencrypted/preserve/update_engine/prefs/rollback-version",
      "unencrypted/preserve/update_engine/prefs/last-active-ping-day",
      "unencrypted/preserve/update_engine/prefs/last-roll-call-ping-day",

      // Preserve the device last active dates to Private Set Computing (psm).
      "unencrypted/preserve/last_active_dates",
      // Preserve pre-installed demo mode resources for offline Demo Mode.
      "unencrypted/cros-components/offline-demo-mode-resources/image.squash",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.json",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.sig.1",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "imageloader.sig.2",
      "unencrypted/cros-components/offline-demo-mode-resources/"
      "manifest.fingerprint",
      "unencrypted/cros-components/offline-demo-mode-resources/manifest.json",
      "unencrypted/cros-components/offline-demo-mode-resources/table",
      // Preserve the latest GSC crash ID to prevent uploading previously seen
      // GSC
      // crashes on every boot.
      "unencrypted/preserve/gsc_prev_crash_log_id",
      // Preserve the files used to identify ChromeOS Flex devices.
      "unencrypted/preserve/flex/flex_id",
      "unencrypted/preserve/flex/flex_state_key",
  });
}

// For the Chromad to cloud migration, we store a flag file to indicate that
// some OOBE screens should be skipped after the device is powerwashed.
std::set<std::string> GetADMigrationPaths() {
  return std::set<std::string>(
      {"unencrypted/preserve/chromad_migration_skip_oobe"});
}

std::set<std::string> GetRollbackWipePaths() {
  return std::set<std::string>({
      // For rollback wipes, we preserve the rollback metrics file and
      // additional
      // data as defined in oobe_config/rollback_data.proto.
      "unencrypted/preserve/enterprise-rollback-metrics-data",
      // Devices produced >= 2023 use the new rollback data
      // ("rollback_data_tpm") encryption.
      "unencrypted/preserve/rollback_data_tpm",
      // TODO(b/263065223) Preservation of the old format ("rollback_data") can
      // be removed when all devices produced before 2023 are EOL.
      "unencrypted/preserve/rollback_data",
  });
}

std::set<std::string> GetRmaWipePaths() {
  return std::set<std::string>({"unencrypted/rma-data/state"});
}

// Test images in the lab enable certain extra behaviors if the
// .labmachine flag file is present.  Those behaviors include some
// important recovery behaviors (cf. the recover_duts upstart job).
// We need those behaviors to survive across power wash, otherwise,
// the current boot could wind up as a black hole.
std::set<std::string> GetDebugBuildPaths() {
  return std::set<std::string>({".labmachine"});
}

std::set<std::string> GetDevModePaths() {
  return std::set<std::string>({"unencrypted/dev_image.block"});
}

}  // namespace

std::set<std::string> GetFactoryPreservationPathList(
    const base::FilePath& mount_path) {
  std::set<std::string> ret;
  base::FileEnumerator crx_enumerator(
      mount_path.Append("unencrypted/import_extensions/extensions"), false,
      base::FileEnumerator::FileType::FILES, "*.crx");
  for (base::FilePath name = crx_enumerator.Next(); !name.empty();
       name = crx_enumerator.Next()) {
    ret.insert(base::FilePath("unencrypted/import_extensions/extensions")
                   .Append(name.BaseName())
                   .value());
  }

  base::FileEnumerator dlc_enumerator(
      mount_path.Append("unencrypted/dlc-factory-images"), false,
      base::FileEnumerator::DIRECTORIES);
  for (base::FilePath dir = dlc_enumerator.Next(); !dir.empty();
       dir = dlc_enumerator.Next()) {
    base::FilePath dlc_image_path =
        base::FilePath("unencrypted/dlc-factory-images")
            .Append(dir.BaseName())
            .Append("package")
            .Append("dlc.img");
    if (base::PathExists(mount_path.Append(dlc_image_path))) {
      ret.insert(dlc_image_path.value());
    }
  }

  return ret;
}

// Generates a list of files that needs to be preserved across powerwash
// and on default_key_stateful setup on first boot.
std::set<std::string> GetPreservationFileList(bool safe_wipe,
                                              bool ad_migration_wipe,
                                              bool rollback_wipe,
                                              bool rma_wipe,
                                              bool debug_build,
                                              bool dev_mode) {
  std::set<std::string> ret;

  if (safe_wipe) {
    for (auto& path : GetSafeModePaths()) {
      ret.insert(path);
    }

    if (ad_migration_wipe) {
      for (auto& path : GetADMigrationPaths()) {
        ret.insert(path);
      }
    }

    if (rollback_wipe) {
      for (auto& path : GetRollbackWipePaths()) {
        ret.insert(path);
      }
    }
  }

  if (rma_wipe) {
    for (auto& path : GetRmaWipePaths()) {
      ret.insert(path);
    }
  }

  if (debug_build) {
    for (auto& path : GetDebugBuildPaths()) {
      ret.insert(path);
    }
  }

  if (dev_mode) {
    for (auto& path : GetDevModePaths()) {
      ret.insert(path);
    }
  }

  return ret;
}

std::set<std::string> GetPreservationFileList() {
  return GetPreservationFileList(true, true, true, true, true, true);
}

std::set<std::string> GetStartupPreseedingPaths() {
  return std::set<std::string>({"unencrypted/preserve/clobber.log",
                                "unencrypted/preserve/clobber-state.log"});
}

}  // namespace libpreservation
