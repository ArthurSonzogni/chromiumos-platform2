// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <sysexits.h>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/files/file_util.h>
#include "gsclog/gsclog.h"

namespace gsclog {
namespace {
constexpr char kCurrentLogExt[] = "gsc.log";
}  // namespace

GscLog::GscLog(const base::FilePath& log_dir)
    : default_trunks_factory_(std::make_unique<trunks::TrunksFactoryImpl>()) {
  log_ = log_dir.Append(kCurrentLogExt);
}

int GscLog::Fetch() {
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

  if (!base::AppendToFile(log_, logs)) {
    PLOG(ERROR) << "Could not append to log file";
    return EX_CANTCREAT;
  }

  return EX_OK;
}

}  // namespace gsclog
