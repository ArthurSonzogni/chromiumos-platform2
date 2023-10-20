// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/concierge_daemon.h"

int main(int argc, char** argv) {
  return vm_tools::concierge::ConciergeDaemon::Run(argc, argv);
}
