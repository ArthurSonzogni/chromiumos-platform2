// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/daemon/hps_daemon.h"

#include <stdint.h>
#include <string.h>

#include <base/check.h>
#include <base/command_line.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "hps/hal/fake_dev.h"
#include "hps/hal/ftdi.h"
#include "hps/hal/i2c.h"
#include "hps/hal/uart.h"

int main(int argc, char* argv[]) {
  DEFINE_string(bus, "/dev/i2c-2", "I2C device");
  DEFINE_uint32(addr, 0x30, "I2C address of module");
  DEFINE_bool(ftdi, false, "Use FTDI connection");
  DEFINE_bool(test, false, "Use internal test fake");
  DEFINE_string(uart, "", "Use UART connection");
  DEFINE_bool(skipboot, false, "Skip boot sequence");
  DEFINE_uint32(version, 0, "Firmware version");
  DEFINE_string(mcu_path, "", "MCU firmware file");
  DEFINE_string(spi_path, "", "SPI firmware file");
  brillo::FlagHelper::Init(argc, argv, "hps_daemon - HPS services daemon");

  // Always log to syslog and log to stderr if we are connected to a tty.
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  base::ThreadPoolInstance::CreateAndStartWithDefaultParams(
      "hps_daemon_thread_pool");

  // Determine the hardware connection.
  std::unique_ptr<hps::DevInterface> dev;
  if (FLAGS_ftdi) {
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
  CHECK(dev) << "Hardware device failed to initialise";
  LOG(INFO) << "Starting HPS Service.";
  auto hps = std::make_unique<hps::HPS>(std::move(dev));
  if (FLAGS_skipboot) {
    hps->SkipBoot();
  } else {
    hps->Init(FLAGS_version, base::FilePath(FLAGS_mcu_path),
              base::FilePath(FLAGS_spi_path));
    // TODO(amcrae): Likely need a better recovery mechanism.
    CHECK(hps->Boot()) << "Hardware failed to boot";
  }
  int exit_code = hps::HpsDaemon(std::move(hps)).Run();
  LOG(INFO) << "HPS Service ended with exit_code=" << exit_code;

  return exit_code;
}
