// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/tcpc.h"

#include <fcntl.h>

#include <memory>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/values.h>
#include <libec/pd_chip_info_command.h>

namespace runtime_probe {
namespace {

// Times to retry the ec command if timeout.
const int kEcCmdNumAttempts = 10;
// The maximum port number of TCPC.
const uint8_t kMaxPortCount = 255;

uint32_t RunCommandRetry(const base::ScopedFD& ec_dev,
                         ec::PdChipInfoCommandV0* cmd) {
  for (int i = 0; i < kEcCmdNumAttempts; ++i) {
    // We expected the command runs successfully or returns invalid param error.
    if (cmd->Run(ec_dev.get()) || cmd->Result() == EC_RES_INVALID_PARAM)
      return cmd->Result();
  }
  LOG(ERROR) << "Failed to run ec command, error code: " << cmd->Result();
  return cmd->Result();
}

}  // namespace

std::unique_ptr<ec::PdChipInfoCommandV0> TcpcFunction::GetPdChipInfoCommandV0(
    uint8_t port) const {
  // Set |live| to 1 to read live chip values instead of hard-coded values.
  return std::make_unique<ec::PdChipInfoCommandV0>(port, /*live=*/1);
}

TcpcFunction::DataType TcpcFunction::EvalImpl() const {
  DataType result{};
  base::ScopedFD ec_dev(open(ec::kCrosEcPath, O_RDWR));

  for (uint8_t port = 0; port < kMaxPortCount; ++port) {
    auto cmd = GetPdChipInfoCommandV0(port);
    if (RunCommandRetry(ec_dev, cmd.get()) != EC_RES_SUCCESS)
      break;

    base::Value val{base::Value::Type::DICTIONARY};
    val.SetIntKey("port", port);
    val.SetIntKey("vendor_id", cmd->VendorId());
    val.SetIntKey("product_id", cmd->ProductId());
    val.SetIntKey("device_id", cmd->DeviceId());
    result.push_back(std::move(val));
  }

  return result;
}

}  // namespace runtime_probe
