// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Main command program.
 */

#include <iostream>
#include <memory>

#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include <base/time/time.h>

#include "hps/lib/fake_dev.h"
#include "hps/lib/ftdi.h"
#include "hps/lib/hps.h"
#include "hps/lib/i2c.h"
#include "hps/lib/retry.h"
#include "hps/util/command.h"

// Static allocation of global command list head.
Command* Command::list_;

int main(int argc, char* argv[]) {
  int c;
  int bus = 2;
  int addr = 0x30;
  int retries = 0;
  int delay = 10;  // In milliseconds
  bool use_ftdi = false;
  bool use_fake = false;

  while (true) {
    int option_index = 0;
    static struct option long_options[] = {
        {"bus", required_argument, 0, 'b'},
        {"addr", required_argument, 0, 'a'},
        {"ftdi", no_argument, 0, 'f'},
        {"retries", required_argument, 0, 'r'},
        {"retry_delay", required_argument, 0, 'd'},
        {0, 0, 0, 0}};
    c = getopt_long(argc, argv, "a:b:r:d:ft", long_options, &option_index);
    if (c == -1) {
      break;
    }
    switch (c) {
      case 'a':
        addr = atoi(optarg);
        break;

      case 'b':
        bus = atoi(optarg);
        break;

      case 'f':
        use_ftdi = true;
        break;

      case 't':
        use_fake = true;
        break;

      case 'r':
        retries = atoi(optarg);
        break;

      case 'd':
        delay = atoi(optarg);
        break;

      default:
        return 1;
    }
  }
  if (optind >= argc) {
    Command::ShowHelp();
    return 1;
  }
  std::unique_ptr<hps::DevInterface> dev;
  if (use_ftdi) {
    auto ftdi = new hps::Ftdi(addr);
    if (!ftdi->Init()) {
      return 1;
    }
    dev.reset(ftdi);
  } else if (use_fake) {
    // The fake has to be started.
    auto fd = new hps::FakeDev;
    // TODO(amcrae): Allow passing error flags.
    fd->Start(0);
    dev.reset(fd);
  } else {
    auto i2c = new hps::I2CDev(bus, addr);
    if (i2c->Open() < 0) {
      return 1;
    }
    dev.reset(i2c);
  }
  if (retries > 0) {
    // If retries are required, add a retry device.
    std::cout << "Enabling retries: " << retries
              << ", delay per retry: " << delay << " ms" << std::endl;
    auto baseDevice = dev.release();
    dev.reset(new hps::RetryDev(baseDevice, retries,
                                base::TimeDelta::FromMilliseconds(delay)));
  }
  auto hps = new hps::HPS(dev.get());
  // Pass new argc/argv to the command for any following arguments.
  // argv[0] is command name.
  int cmd_argc = argc - optind;
  char** cmd_argv = &argv[optind];
  return Command::Execute(cmd_argv[0], hps, cmd_argc, cmd_argv);
}
