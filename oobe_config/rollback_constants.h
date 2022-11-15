// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_ROLLBACK_CONSTANTS_H_
#define OOBE_CONFIG_ROLLBACK_CONSTANTS_H_

namespace oobe_config {

inline constexpr char kStatefulPartitionPath[] = "/mnt/stateful_partition/";
// oobe_config_prepare_save copied the files into this directory.
inline constexpr char kSaveTempPath[] = "/var/lib/oobe_config_save/";
// oobe_config_finish_restore is expecting the files in this directory.
inline constexpr char kRestoreTempPath[] = "/var/lib/oobe_config_restore/";

// The rollback data is stored here on unencrypted stateful as an encrypted
// proto. This is the file which is preserved over powerwash.
inline constexpr char kUnencryptedStatefulRollbackDataFile[] =
    "/mnt/stateful_partition/unencrypted/preserve/rollback_data";

// The path to the file that indicates if OOBE has completed.
inline constexpr char kOobeCompletedFile[] = "/home/chronos/.oobe_completed";
inline constexpr char kOobeCompletedFileName[] = ".oobe_completed";

// The path to the file that indicates if metrics reporting is enabled.
inline constexpr char kMetricsReportingEnabledFile[] =
    "/home/chronos/Consent To Send Stats";
inline constexpr char kMetricsReportingEnabledFileName[] =
    "Consent To Send Stats";

inline constexpr char kOobeConfigSaveUsername[] = "oobe_config_save";
inline constexpr char kRootUsername[] = "root";
inline constexpr char kPreserveGroupName[] = "preserve";

}  // namespace oobe_config

#endif  // OOBE_CONFIG_ROLLBACK_CONSTANTS_H_
