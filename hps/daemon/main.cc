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
#include "hps/hal/mcp.h"
#include "hps/hal/uart.h"
#include "hps/hps_impl.h"
#include "hps/utils.h"

int main(int argc, char* argv[]) {
  base::AtExitManager at_exit;

  DEFINE_string(bus, "/dev/i2c-2", "I2C device");
  DEFINE_uint32(addr, 0x30, "I2C address of module");
  DEFINE_uint32(speed, 200, "I2C bus speed in KHz");
  DEFINE_bool(ftdi, false, "Use FTDI connection");
  DEFINE_bool(mcp, false, "Use MCP2221A connection");
  DEFINE_bool(test, false, "Use internal test fake");
  DEFINE_string(uart, "", "Use UART connection");
  DEFINE_bool(skipboot, false, "Skip boot sequence");
  DEFINE_int64(version, -1, "Override MCU firmware file version");
  DEFINE_string(mcu_path, "", "MCU firmware file");
  DEFINE_string(spi_path, "", "SPI firmware file");
  DEFINE_uint32(poll_timer_ms, 500,
                "How frequently to poll HPS hardware for results (in ms).");
  brillo::FlagHelper::Init(argc, argv, "hps_daemon - HPS services daemon");

  // Always log to syslog and log to stderr if we are connected to a tty.
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  base::ThreadPoolInstance::CreateAndStartWithDefaultParams(
      "hps_daemon_thread_pool");

  uint32_t version;
  if (FLAGS_version < 0) {
    if (!hps::ReadVersionFromFile(base::FilePath(FLAGS_mcu_path), &version)) {
      return 1;
    }
  } else {
    version = base::checked_cast<uint32_t>(FLAGS_version);
  }

  // Determine the hardware connection.
  std::unique_ptr<hps::DevInterface> dev;
  uint8_t addr = base::checked_cast<uint8_t>(FLAGS_addr);
  if (FLAGS_mcp) {
    dev = hps::Mcp::Create(addr, FLAGS_speed);
  } else if (FLAGS_ftdi) {
    dev = hps::Ftdi::Create(addr, FLAGS_speed);
  } else if (FLAGS_test) {
    // Initialise the fake device as already booted so that
    // features can be enabled/disabled.
    auto fake = hps::FakeDev::Create();
    fake->SkipBoot();
    dev = fake->CreateDevInterface();
  } else if (!FLAGS_uart.empty()) {
    dev = hps::Uart::Create(FLAGS_uart.c_str());
  } else {
    dev = hps::I2CDev::Create(FLAGS_bus.c_str(), addr);
  }
  CHECK(dev) << "Hardware device failed to initialise";
  LOG(INFO) << "Starting HPS Service.";
  auto hps = std::make_unique<hps::HPS_impl>(std::move(dev));
  if (!FLAGS_skipboot) {
    hps->Init(version, base::FilePath(FLAGS_mcu_path),
              base::FilePath(FLAGS_spi_path));
    // TODO(amcrae): Likely need a better recovery mechanism.
    CHECK(hps->Boot()) << "Hardware failed to boot";
  }
  int exit_code = hps::HpsDaemon(std::move(hps), FLAGS_poll_timer_ms).Run();
  LOG(INFO) << "HPS Service ended with exit_code=" << exit_code;

  return exit_code;
}
