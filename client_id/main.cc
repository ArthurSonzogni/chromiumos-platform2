// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "client_id/client_id.h"

#include <iostream>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <brillo/syslog_logging.h>

int main() {
  brillo::InitLog(brillo::kLogToSyslog);

  client_id::ClientIdGenerator client_id_generator(base::FilePath("/"));
  auto client_id = client_id_generator.GenerateAndSaveClientId();
  if (!client_id) {
    LOG(ERROR) << "Couldn't save client_id. Exiting.";
    return 1;
  }
  std::cout << client_id.value() << std::endl;
  LOG(INFO) << "client_id ran successfully. Exiting.";
  return 0;
}
