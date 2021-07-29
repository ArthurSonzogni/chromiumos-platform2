// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/rollback_constants.h"

namespace oobe_config {

const base::FilePath kStatefulPartition =
    base::FilePath("/mnt/stateful_partition/");

const base::FilePath kSaveTempPath =
    base::FilePath("/var/lib/oobe_config_save/");
const base::FilePath kRestoreTempPath =
    base::FilePath("/var/lib/oobe_config_restore/");
const base::FilePath kUnencryptedStatefulRollbackDataPath = base::FilePath(
    "/mnt/stateful_partition/unencrypted/preserve/rollback_data");
const base::FilePath kEncryptedStatefulRollbackDataPath =
    base::FilePath("/var/lib/oobe_config_restore/rollback_data");

const base::FilePath kRollbackSaveMarkerFile =
    base::FilePath("/mnt/stateful_partition/.save_rollback_data");

const base::FilePath kOobeCompletedFile =
    base::FilePath("/home/chronos/.oobe_completed");
const char kOobeCompletedFileName[] = ".oobe_completed";

const base::FilePath kMetricsReportingEnabledFile =
    base::FilePath("/home/chronos/Consent To Send Stats");
const char kMetricsReportingEnabledFileName[] = "Consent To Send Stats";

const char kOobeConfigSaveUsername[] = "oobe_config_save";
const char kRootUsername[] = "root";
const char kPreserveGroupName[] = "preserve";

const base::FilePath kDataSavedFile =
    base::FilePath("/var/lib/oobe_config_save/.data_saved");

const base::FilePath kRollbackDataForPmsgFile =
    base::FilePath("/var/lib/oobe_config_save/data_for_pstore");
const base::FilePath kPstorePath = base::FilePath("/sys/fs/pstore");

}  // namespace oobe_config
