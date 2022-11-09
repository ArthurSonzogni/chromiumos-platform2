// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>

#include "ml_core/dlc/dlc_loader.h"

int main(int argc, char* argv[]) {
  cros::DlcLoader client;
  client.Run();
  LOG(INFO) << client.DlcLoaded();
  LOG(INFO) << client.GetDlcRootPath();
}
