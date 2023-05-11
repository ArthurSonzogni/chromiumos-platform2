// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/command_line.h>

#include "arc/keymint/daemon.h"

int main(int argc, char** argv) {
  // arc-keymintd takes no command line arguments.
  base::CommandLine::Init(argc, argv);

  // TODO(b/247941366): Add logging support.

  LOG(INFO) << "Running Daemon";
  arc::keymint::Daemon daemon;
  return daemon.Run();
}
