// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FUNCTIONS_INPUT_DEVICE_H_
#define RUNTIME_PROBE_FUNCTIONS_INPUT_DEVICE_H_

#include <memory>
#include <string>

#include "runtime_probe/probe_function.h"

namespace runtime_probe {

// Probe input devices on the system.
//
// This function takes one optional string argument "device_type", which could
// be "stylus", "touchpad", "touchscreen", and "unknown".  If "device_type" is
// not specified, this function will output all input devices.
//
// Example probe statement::
//   {
//     "device_type": "touchscreen"
//   }
class InputDeviceFunction : public PrivilegedProbeFunction {
 public:
  NAME_PROBE_FUNCTION("input_device");

  static std::unique_ptr<InputDeviceFunction> FromKwargsValue(
      const base::Value& dict_value);

 private:
  DataType EvalImpl() const override;

  std::string device_type_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FUNCTIONS_INPUT_DEVICE_H_
