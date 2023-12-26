// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <sysexits.h>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <brillo/files/file_util.h>
#include "gsclog/gsclog.h"

namespace gsclog {
namespace {
constexpr char kCurrentLogExt[] = "gsc.log";
}  // namespace

using base::File;

GscLog::GscLog(const base::FilePath& log_dir)
    : log_path_(base::FilePath(log_dir.Append(kCurrentLogExt))),
      default_trunks_factory_(std::make_unique<trunks::TrunksFactoryImpl>()) {}

int GscLog::Fetch() {
  File log(log_path_, File::Flags::FLAG_OPEN_ALWAYS | File::Flags::FLAG_APPEND);
  if (!log.IsValid()) {
    LOG(ERROR) << "Failed to open file: "
               << File::ErrorToString(log.error_details());
    return EX_NOPERM;
  }
  if (log.Lock(File::LockMode::kExclusive) != File::Error::FILE_OK) {
    // If another instance of gsclog was using the file, sleep 1 second and try
    // again. If that fails, bail.
    const base::TimeDelta sleep_interval = base::Seconds(1);
    sleep(sleep_interval.InSeconds());
    File::Error error = log.Lock(File::LockMode::kExclusive);
    if (error != File::Error::FILE_OK) {
      LOG(ERROR) << "Failed to lock log file: " << File::ErrorToString(error);
      return EX_UNAVAILABLE;
    }
  }

  if (!default_trunks_factory_->Initialize()) {
    LOG(ERROR) << "Failed to initialize trunks.";
    return EX_UNAVAILABLE;
  }
  trunks_factory_ = default_trunks_factory_.get();
  trunks_utility_ = trunks_factory_->GetTpmUtility();

  std::string logs;
  if (trunks_utility_->GetConsoleLogs(&logs) != trunks::TPM_RC_SUCCESS) {
    LOG(ERROR) << "Failed to get console logs.";
    return EX_NOPERM;
  }

  if (!log.Write(0, logs.c_str(), logs.length())) {
    PLOG(ERROR) << "Could not append to log file";
    return EX_CANTCREAT;
  }

  return EX_OK;
}

}  // namespace gsclog
