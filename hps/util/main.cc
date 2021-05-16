// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/*
 * Main command program.
 */

#include <iostream>
#include <memory>
#include <utility>

#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include <base/command_line.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>

#include "hps/lib/fake_dev.h"
#include "hps/lib/ftdi.h"
#include "hps/lib/hps.h"
#include "hps/lib/i2c.h"
#include "hps/lib/retry.h"
#include "hps/lib/uart.h"
#include "hps/util/command.h"

// Static allocation of global command list head.
Command* Command::list_;

int main(int argc, char* argv[]) {
  DEFINE_uint32(bus, 2, "I2C bus");
  DEFINE_uint32(addr, 0x30, "I2C address of module");
  DEFINE_uint32(retries, 0, "Max I2C retries");
  DEFINE_uint32(retry_delay, 10, "Delay in ms between retries");
  DEFINE_bool(ftdi, false, "Use FTDI connection");
  DEFINE_bool(test, false, "Use internal test fake");
  DEFINE_string(uart, "", "Use UART connection");
  brillo::FlagHelper::Init(argc, argv, "HPS tool.");

  const logging::LoggingSettings ls;
  logging::InitLogging(ls);

  auto args = base::CommandLine::ForCurrentProcess()->GetArgs();
  if (args.size() == 0) {
    Command::ShowHelp();
    return 1;
  }
  std::unique_ptr<hps::DevInterface> dev;
  if (FLAGS_ftdi) {
    auto ftdi = std::make_unique<hps::Ftdi>(FLAGS_addr);
    if (!ftdi->Init()) {
      return 1;
    }
    dev = std::move(ftdi);
  } else if (FLAGS_test) {
    // The fake has to be started.
    auto fd = std::make_unique<hps::FakeDev>();
    // TODO(amcrae): Allow passing error flags.
    fd->Start(hps::FakeDev::Flags::kSkipBoot);
    dev = std::move(fd);
  } else if (!FLAGS_uart.empty()) {
    auto uart = std::make_unique<hps::Uart>(FLAGS_uart.c_str());
    if (!uart->Open()) {
      return 1;
    }
    dev = std::move(uart);
  } else {
    auto i2c = std::make_unique<hps::I2CDev>(FLAGS_bus, FLAGS_addr);
    if (i2c->Open() < 0) {
      return 1;
    }
    dev = std::move(i2c);
  }
  if (FLAGS_retries > 0) {
    // If retries are required, add a retry device.
    std::cout << "Enabling retries: " << FLAGS_retries
              << ", delay per retry: " << FLAGS_retry_delay << " ms"
              << std::endl;
    auto baseDevice = std::move(dev);
    dev = std::make_unique<hps::RetryDev>(
        std::move(baseDevice), FLAGS_retries,
        base::TimeDelta::FromMilliseconds(FLAGS_retry_delay));
  }
  auto hps = std::make_unique<hps::HPS>(std::move(dev));
  // Pass args to the command for any following arguments.
  // args[0] is command name.
  return Command::Execute(args[0].c_str(), std::move(hps), args);
}
