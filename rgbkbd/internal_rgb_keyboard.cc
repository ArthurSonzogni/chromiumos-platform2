// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rgbkbd/internal_rgb_keyboard.h"

#include <fcntl.h>
#include <stdint.h>
#include <sstream>
#include <string>
#include <vector>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <libec/rgb_keyboard_command.h>

namespace rgbkbd {

namespace {

constexpr char kEcPath[] = "/dev/cros_ec";

std::string CreateRgbLogString(uint8_t r, uint8_t g, uint8_t b) {
  std::stringstream rgb_log;
  rgb_log << " R: " << static_cast<int>(r) << "G: " << static_cast<int>(g)
          << "B: " << static_cast<int>(b);
  return rgb_log.str();
}
}  // namespace

bool InternalRgbKeyboard::SetKeyColor(uint32_t key,
                                      uint8_t r,
                                      uint8_t g,
                                      uint8_t b) {
  auto fd = base::ScopedFD(open(kEcPath, O_RDWR | O_CLOEXEC));
  if (!fd.is_valid()) {
    LOG(ERROR) << "rgbkbd: Failed to open FD for EC while calling SetKeyColor";
    return false;
  }

  struct rgb_s color = {r, g, b};
  ec::RgbkbdSetColorCommand command(key, std::vector<struct rgb_s>{color});

  auto success = command.Run(fd.get());
  if (success) {
    LOG(INFO) << "rgbkbd: Call to ec::RgbkbdSetColorCommand SUCCEEDED"
              << "with Key: " << key << CreateRgbLogString(r, g, b);
  } else {
    LOG(ERROR) << "rgbkbd: Call to ec::RgbkbdSetColorCommand FAILED"
               << "with Key: " << key << CreateRgbLogString(r, g, b);
  }
  return success;
}

bool InternalRgbKeyboard::SetAllKeyColors(uint8_t r, uint8_t g, uint8_t b) {
  auto fd = base::ScopedFD(open(kEcPath, O_RDWR | O_CLOEXEC));
  if (!fd.is_valid()) {
    LOG(ERROR)
        << "rgbkbd: Failed to open FD for EC while calling SetAllKeyColors";
    return false;
  }

  struct rgb_s color = {r, g, b};
  auto command = ec::RgbkbdCommand::Create(EC_RGBKBD_SUBCMD_CLEAR, color);

  auto success = command->Run(fd.get());
  if (success) {
    LOG(INFO) << "rgbkbd: Call to ec::RgbkbdCommand SUCCEEDED:"
              << CreateRgbLogString(r, g, b);
  } else {
    LOG(ERROR) << "rgbkbd: Call to ec::RgbkbdCommand FAILED:"
               << CreateRgbLogString(r, g, b);
  }
  return success;
}

bool InternalRgbKeyboard::GetRgbKeyboardCapabilities() {
  auto fd = base::ScopedFD(open(kEcPath, O_RDWR | O_CLOEXEC));
  if (!fd.is_valid()) {
    LOG(ERROR) << "rgbkbd: Failed to open FD for EC while attempting to "
                  "determine keyboard capabilities";
    return false;
  }

  struct rgb_s color = {0, 0, 0};
  auto command = ec::RgbkbdCommand::Create(EC_RGBKBD_SUBCMD_CLEAR, color);

  auto success = command->Run(fd.get());
  if (success) {
    LOG(INFO) << "rgbkbd: Call to ec::RgbkbdCommand SUCCEEDED for "
                 "GetRgbKeyboardCapabilities";
  } else {
    LOG(ERROR) << "rgbkbd: Call to ec::RgbkbdCommand FAILED for "
                  "GetRgbKeyboardCapabilities";
  }
  return success;
}
}  // namespace rgbkbd
