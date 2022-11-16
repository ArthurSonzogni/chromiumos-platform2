// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_EC_I2C_H_
#define RUNTIME_PROBE_FUNCTIONS_EC_I2C_H_

#include <memory>
#include <string>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/values.h>

#include "runtime_probe/probe_function.h"

namespace ec {
class I2cReadCommand;
};

namespace runtime_probe {

// Read data from an I2C register on EC (embedded controller).
// This probe function takes the following arguments:
//   i2c_bus: The port of the I2C connected to EC.
//   chip_addr: The I2C address
//   data_addr: The register offset.
//   size: Return bits, it can be either 8 or 16.
//
// More details can be found under command "ectool i2cread help"
class EcI2cFunction : public PrivilegedProbeFunction {
  using PrivilegedProbeFunction::PrivilegedProbeFunction;

 public:
  NAME_PROBE_FUNCTION("ec_i2c");

  // Define a parser for this function.
  //
  // @args dict_value: a JSON dictionary to parse arguments from.
  //
  // @return pointer to new `EcI2cFunction` instance on success, nullptr
  //   otherwise.
  template <typename T>
  static std::unique_ptr<T> FromKwargsValue(const base::Value& dict_value) {
    PARSE_BEGIN();
    PARSE_ARGUMENT(i2c_bus);
    PARSE_ARGUMENT(chip_addr);
    PARSE_ARGUMENT(data_addr);
    PARSE_ARGUMENT(size, 8);
    if (instance->size_ != 8 && instance->size_ != 16) {
      LOG(ERROR) << "\"size\" should be 8 or 16.";
      return nullptr;
    }
    PARSE_END();
  }

 private:
  DataType EvalImpl() const override;

  virtual std::unique_ptr<ec::I2cReadCommand> GetI2cReadCommand() const;

  virtual base::ScopedFD GetEcDevice() const;

  int i2c_bus_;
  int chip_addr_;
  int data_addr_;
  int size_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_EC_I2C_H_
