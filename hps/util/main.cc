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

#include <base/check.h>
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
  DEFINE_string(bus, "/dev/i2c-2", "I2C device");
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
    dev = hps::Ftdi::Create(FLAGS_addr);
  } else if (FLAGS_test) {
    dev = hps::FakeDev::Create(hps::FakeDev::Flags::kNone);
  } else if (!FLAGS_uart.empty()) {
    dev = hps::Uart::Create(FLAGS_uart.c_str());
  } else {
    dev = hps::I2CDev::Create(FLAGS_bus.c_str(), FLAGS_addr);
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
