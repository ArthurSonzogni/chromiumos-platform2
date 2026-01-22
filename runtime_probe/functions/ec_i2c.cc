// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/ec_i2c.h"

#include <fcntl.h>

#include <optional>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/values.h>
#include <libec/i2c_read_command.h>

#include "runtime_probe/system/context.h"

namespace runtime_probe {

namespace {
constexpr int kEcCmdNumAttempts = 10;
constexpr char kCrosEcName[] = "cros_ec";
constexpr char kCrosIshName[] = "cros_ish";
constexpr char kCrosEcPath[] = "dev/cros_ec";
constexpr char kCrosIshPath[] = "dev/cros_ish";
}  // namespace

std::unique_ptr<ec::I2cReadCommand> EcI2cFunction::GetI2cReadCommand() const {
  return ec::I2cReadCommand::Create(
      static_cast<uint8_t>(i2c_bus_), static_cast<uint8_t>(chip_addr_),
      static_cast<uint8_t>(data_addr_), static_cast<uint8_t>(size_ / 8));
}

std::optional<base::ScopedFD> EcI2cFunction::GetEcDevice() const {
  std::string device_path;
  if (ec_type_ == kCrosEcName) {
    device_path = kCrosEcPath;
  } else if (ec_type_ == kCrosIshName) {
    device_path = kCrosIshPath;
  } else {
    LOG(ERROR) << "Got invalid EC type: " << ec_type_;
    return std::nullopt;
  }

  auto dev_path = Context::Get()->root_dir().Append(device_path);
  if (!base::PathExists(dev_path)) {
    LOG(ERROR) << dev_path << " doesn't exist.";
    return std::nullopt;
  }

  return base::ScopedFD(open(dev_path.value().c_str(), O_RDWR));
}

bool EcI2cFunction::PostParseArguments() {
  if (size_ != 8 && size_ != 16 && size_ != 32) {
    LOG(ERROR) << "function " << GetFunctionName()
               << " argument \"size\" should be 8, 16 or 32.";
    return false;
  }
  return true;
}

EcI2cFunction::DataType EcI2cFunction::EvalImpl() const {
  auto ec_dev = GetEcDevice();
  if (!ec_dev.has_value()) {
    LOG(ERROR) << "Failed to get EC device";
    return {};
  }

  auto cmd = GetI2cReadCommand();
  if (!cmd) {
    LOG(ERROR) << "Failed to create ec::I2cReadCommand";
    return {};
  }
  if (!cmd->RunWithMultipleAttempts(ec_dev->get(), kEcCmdNumAttempts)) {
    LOG(ERROR) << "Failed to read I2C data from EC";
    return {};
  }
  if (cmd->I2cStatus()) {
    LOG(ERROR) << "Unexpected I2C status: "
               << static_cast<int>(cmd->I2cStatus());
    return {};
  }

  DataType result{};
  base::Value::Dict dv{};
  if (size_ == 8 || size_ == 16 || size_ == 32) {
    uint32_t data = cmd->Data();
    if (data > INT_MAX) {
      dv.Set("data", base::NumberToString(data));
    } else {
      dv.Set("data", static_cast<int>(data));
    }
  }
  result.Append(std::move(dv));
  return result;
}

}  // namespace runtime_probe
