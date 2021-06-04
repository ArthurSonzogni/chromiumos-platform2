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

#include "hps/hal/fake_dev.h"
#include "hps/hal/ftdi.h"
#include "hps/hal/i2c.h"
#include "hps/hal/mcp.h"
#include "hps/hal/retry.h"
#include "hps/hal/uart.h"
#include "hps/hps.h"
#include "hps/util/command.h"

// Static allocation of global command list head.
Command* Command::list_;

int main(int argc, char* argv[]) {
  DEFINE_string(bus, "/dev/i2c-2", "I2C device");
  DEFINE_uint32(addr, 0x30, "I2C address of module");
  DEFINE_uint32(retries, 0, "Max I2C retries");
  DEFINE_uint32(retry_delay, 10, "Delay in ms between retries");
  DEFINE_bool(ftdi, false, "Use FTDI connection");
  DEFINE_bool(mcp, false, "Use MCP2221A connection");
  DEFINE_bool(test, false, "Use internal test fake");
  DEFINE_string(uart, "", "Use UART connection");
  brillo::FlagHelper::Init(argc, argv,
                           "usage: hps [ --mcp | --ftdi | --test | --bus "
                           "<i2c-bus> ] [ --addr <i2c-addr> ]\n"
                           "           <command> <command arguments>\n\n" +
                               Command::GetHelp());

  const logging::LoggingSettings ls;
  logging::InitLogging(ls);

  auto args = base::CommandLine::ForCurrentProcess()->GetArgs();
  if (args.size() == 0) {
    std::cerr << "no command, " << Command::GetHelp();
    return 1;
  }
  std::unique_ptr<hps::DevInterface> dev;
  if (FLAGS_mcp) {
    dev = hps::Mcp::Create(FLAGS_addr);
  } else if (FLAGS_ftdi) {
    dev = hps::Ftdi::Create(FLAGS_addr);
  } else if (FLAGS_test) {
    // Initialise the fake device as already booted so that
    // features can be enabled/disabled.
    auto fake = hps::FakeDev::Create();
    fake->SkipBoot();
    dev = fake->CreateDevInterface();
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
