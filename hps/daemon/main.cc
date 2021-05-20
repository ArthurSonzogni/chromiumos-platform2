// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hps/daemon/hps_daemon.h"

#include <stdint.h>
#include <string.h>

#include <base/check.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/task/thread_pool/thread_pool_instance.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "hps/lib/fake_dev.h"
#include "hps/lib/ftdi.h"
#include "hps/lib/i2c.h"
#include "hps/lib/uart.h"

namespace {

// Extract I2C address from env I2C_ADDR.
uint8_t GetI2CAddress() {
  const char* i2c_addr = getenv("I2C_ADDR");
  CHECK(i2c_addr) << "Missing I2C address (I2C_ADDR)";
  unsigned address;
  CHECK(base::StringToUint(i2c_addr, &address)) << "Illegal I2C address";
  return address;
}

}  //  namespace

int main(int argc, char* argv[]) {
  brillo::FlagHelper::Init(argc, argv, "hps_daemon - HPS services daemon");

  // Always log to syslog and log to stderr if we are connected to a tty.
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  base::ThreadPoolInstance::CreateAndStartWithDefaultParams(
      "hps_daemon_thread_pool");

  // Retrieve the DevInterface provider string.
  const char* dev_type = getenv("HPS_HW");
  CHECK(dev_type) << "Missing hardware device (HPS_HW)";
  // Create the selected h/w device.
  std::unique_ptr<hps::DevInterface> dev;
  if (strcmp(dev_type, "ftdi") == 0) {
    dev = hps::Ftdi::Create(GetI2CAddress());
  } else if (strcmp(dev_type, "test") == 0) {
    auto fake = hps::FakeHps::Create();
    fake->SkipBoot();
    dev = fake->CreateDevInterface();
  } else if (strcmp(dev_type, "uart") == 0) {
    const char* uart_dev = getenv("UART_DEV");
    CHECK(uart_dev) << "Missing UART device (UART_DEV)";
    dev = hps::Uart::Create(uart_dev);
  } else if (strcmp(dev_type, "i2c") == 0) {
    const char* i2c_dev = getenv("I2C_BUS");
    CHECK(i2c_dev) << "Missing I2C device (I2C_BUS)";
    dev = hps::I2CDev::Create(i2c_dev, GetI2CAddress());
  } else {
    LOG(FATAL) << "No matching hardware device for HPS_HW (" << dev_type << ")";
  }
  CHECK(dev) << "Hardware device failed to initialise";
  LOG(INFO) << "Starting HPS Service.";
  auto hps = std::make_unique<hps::HPS>(std::move(dev));
  int exit_code = hps::HpsDaemon(std::move(hps)).Run();
  LOG(INFO) << "HPS Service ended with exit_code=" << exit_code;

  return exit_code;
}
