// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_UTILS_INPUT_DEVICE_H_
#define RUNTIME_PROBE_UTILS_INPUT_DEVICE_H_

#include <bitset>
#include <memory>
#include <string>
#include <vector>

#include <base/values.h>

namespace runtime_probe {

class InputDeviceImpl {
 public:
  // Returns a input device parsed from lines from procfs.
  static std::unique_ptr<InputDeviceImpl> From(
      const std::vector<std::string>& lines);

  // Determines the device is a stylus.
  bool IsStylusDevice() const;

  // Determines the device is a touchpad.
  bool IsTouchpadDevice() const;

  // Determines the device is a touchscreen.
  bool IsTouchscreenDevice() const;

  // Gets the device's type.
  std::string type() const;

  std::string bus;
  std::string event;
  std::string name;
  std::string product;
  std::string sysfs;
  std::string vendor;
  std::string version;

  struct Type {
    inline static const std::string UNKNOWN = "unknown";
    inline static const std::string STYLUS = "stylus";
    inline static const std::string TOUCHPAD = "touchpad";
    inline static const std::string TOUCHSCREEN = "touchscreen";
  };

 private:
  static constexpr size_t kEvKeyMax = 0x2ff;
  static constexpr size_t kEvAbsMax = 0xef;
  static constexpr size_t kEvSwMax = 0x0f;
  std::bitset<kEvKeyMax + 1> ev_key;
  std::bitset<kEvAbsMax + 1> ev_abs;
  std::bitset<kEvSwMax + 1> ev_sw;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_UTILS_INPUT_DEVICE_H_
