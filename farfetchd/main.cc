// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <ctime>

#include <base/logging.h>
#include <brillo/syslog_logging.h>

#include "farfetchd/daemon.h"

int main(int argc, char** argv) {
  brillo::InitLog(brillo::kLogToSyslog);

  char date[26];

  time_t now = time(nullptr);
  ctime_r(&now, date);
  LOG(INFO) << "Farfetchd Started At: " << date;
  return farfetchd::Daemon().Run();
}
