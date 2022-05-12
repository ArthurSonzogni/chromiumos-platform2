// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rgbkbd/internal_rgb_keyboard.h"

#include <errno.h>
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
  rgb_log << " R:" << static_cast<int>(r) << " G:" << static_cast<int>(g)
          << " B:" << static_cast<int>(b);
  return rgb_log.str();
}
}  // namespace

bool InternalRgbKeyboard::SetKeyColor(uint32_t key,
                                      uint8_t r,
                                      uint8_t g,
                                      uint8_t b) {
  auto raw_fd = open(kEcPath, O_RDWR | O_CLOEXEC);
  if (raw_fd == -1) {
    auto err = errno;
    LOG(ERROR)
        << "Failed to open FD for EC while calling SetKeyColor with errno="
        << err;
    return false;
  }

  auto fd = base::ScopedFD(raw_fd);
  DCHECK(fd.is_valid());

  struct rgb_s color = {r, g, b};
  ec::RgbkbdSetColorCommand command(key, std::vector<struct rgb_s>{color});

  auto success = command.Run(fd.get());
  if (success) {
    LOG(INFO) << "Setting key color succeeded with key " << key
              << CreateRgbLogString(r, g, b);
  } else {
    LOG(ERROR) << "Setting key color failed with key " << key
               << CreateRgbLogString(r, g, b);
  }
  return success;
}

bool InternalRgbKeyboard::SetAllKeyColors(uint8_t r, uint8_t g, uint8_t b) {
  auto raw_fd = open(kEcPath, O_RDWR | O_CLOEXEC);
  if (raw_fd == -1) {
    auto err = errno;
    LOG(ERROR)
        << "Failed to open FD for EC while calling SetAllKeyColors with errno="
        << err;
    return false;
  }

  auto fd = base::ScopedFD(raw_fd);
  DCHECK(fd.is_valid());
  struct rgb_s color = {r, g, b};
  auto command = ec::RgbkbdCommand::Create(EC_RGBKBD_SUBCMD_CLEAR, color);

  auto success = command->Run(fd.get());
  if (success) {
    LOG(INFO) << "Setting all key colors to" << CreateRgbLogString(r, g, b)
              << " succeeded";
  } else {
    LOG(ERROR) << "Setting all key colors to" << CreateRgbLogString(r, g, b)
               << " failed";
  }
  return success;
}

RgbKeyboardCapabilities InternalRgbKeyboard::GetRgbKeyboardCapabilities() {
  LOG(INFO) << "Checking RgbKeyboardCapabilities by trying to set all keys to "
            << CreateRgbLogString(/*r=*/0, /*g=*/0, /*b=*/0);
  const bool success = SetAllKeyColors(/*r=*/0, /*g=*/0, /*b=*/0);
  // TODO(michaelcheco): Update to support
  // RgbKeyboardCapabilities::kIndividualKey
  return success ? RgbKeyboardCapabilities::kFiveZone
                 : RgbKeyboardCapabilities::kNone;
}
}  // namespace rgbkbd
