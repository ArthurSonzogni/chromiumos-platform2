// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/usb_monitor.h"

#include <string.h>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include "fuzzer/FuzzedDataProvider.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace typecd {

class UsbMonitorFuzzer {
 public:
  UsbMonitorFuzzer() {
    CHECK(scoped_temp_dir_.CreateUniqueTempDir());
    temp_dir_ = scoped_temp_dir_.GetPath();
  }

  void OnDeviceAddedOrRemoved(const base::FilePath& path, bool added) {
    monitor_.OnDeviceAddedOrRemoved(path, added);
  }

  base::ScopedTempDir scoped_temp_dir_;
  base::FilePath temp_dir_;

 private:
  typecd::UsbMonitor monitor_;
};

}  // namespace typecd

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOGGING_ERROR); }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  FuzzedDataProvider data_provider(data, size);
  typecd::UsbMonitorFuzzer fuzzer;

  auto path = fuzzer.temp_dir_.Append("fakepath");
  CHECK_GE(base::CreateDirectory(path), 0);

  // Fill in busnum and devnum with random strings.
  auto busnum = data_provider.ConsumeRandomLengthString();
  auto devnum = data_provider.ConsumeRandomLengthString();
  CHECK_GE(
      base::WriteFile(path.Append("busnum"), busnum.c_str(), busnum.length()),
      0);
  CHECK_GE(
      base::WriteFile(path.Append("devnum"), devnum.c_str(), devnum.length()),
      0);

  // USB device may or may not have Type C port information.
  if (data_provider.ConsumeBool()) {
    auto connector_dir_path = path.Append("port/connector");
    CHECK_GE(base::CreateDirectory(connector_dir_path), 0);
    auto typec_port = data_provider.ConsumeRandomLengthString();
    CHECK_GE(base::WriteFile(connector_dir_path.Append("uevent"),
                             typec_port.c_str(), typec_port.length()),
             0);
  }

  fuzzer.OnDeviceAddedOrRemoved(path, data_provider.ConsumeBool());

  return 0;
}
