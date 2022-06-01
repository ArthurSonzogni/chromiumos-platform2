// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rgbkbd/internal_rgb_keyboard.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/check_op.h>
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

std::unique_ptr<ec::EcUsbEndpointInterface> CreateEcUsbEndpoint() {
  // TODO(michaelcheco): Replace 0x18d1/0x5022 with constants.
  auto endpoint = std::make_unique<ec::EcUsbEndpoint>();
  return endpoint->Init(0x18d1, 0x5022) ? std::move(endpoint)
                                        : std::unique_ptr<ec::EcUsbEndpoint>();
}

base::ScopedFD CreateFileDescriptorForEc() {
  auto raw_fd = open(kEcPath, O_RDWR | O_CLOEXEC);
  if (raw_fd == -1) {
    auto err = errno;
    LOG(ERROR) << "Failed to open FD for EC with errno=" << err;
    return base::ScopedFD();
  }

  return base::ScopedFD(raw_fd);
}

void LogSupportType(RgbKeyboardCapabilities capabilities) {
  switch (capabilities) {
    case RgbKeyboardCapabilities::kNone:
      LOG(INFO) << "Device does not support an internal RGB keyboard";
      break;
    case RgbKeyboardCapabilities::kFiveZone:
      LOG(INFO) << "Device supports five zone keyboard";
      break;
    case RgbKeyboardCapabilities::kIndividualKey:
      LOG(INFO) << "Device supports per-key keyboard over USB";
      break;
  }
}

}  // namespace

bool InternalRgbKeyboard::SetKeyColor(uint32_t key,
                                      uint8_t r,
                                      uint8_t g,
                                      uint8_t b) {
  DCHECK(capabilities_.has_value());
  DCHECK_NE(RgbKeyboardCapabilities::kNone, capabilities_.value());

  struct rgb_s color = {r, g, b};
  ec::RgbkbdSetColorCommand command(key, std::vector<struct rgb_s>{color});

  bool success = false;
  if (capabilities_ == RgbKeyboardCapabilities::kIndividualKey) {
    DCHECK(usb_endpoint_);
    success = command.Run(*usb_endpoint_);
  } else {
    DCHECK(ec_fd_.is_valid());
    success = command.Run(ec_fd_.get());
  }

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
  DCHECK(capabilities_.has_value());
  DCHECK_NE(RgbKeyboardCapabilities::kNone, capabilities_.value());

  struct rgb_s color = {r, g, b};
  auto command = ec::RgbkbdCommand::Create(EC_RGBKBD_SUBCMD_CLEAR, color);
  bool success = false;
  if (capabilities_ == RgbKeyboardCapabilities::kIndividualKey) {
    DCHECK(usb_endpoint_);
    success = command->Run(*usb_endpoint_);
  } else {
    DCHECK(ec_fd_.is_valid());
    success = command->Run(ec_fd_.get());
  }

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
  if (capabilities_.has_value()) {
    return capabilities_.value();
  }

  DCHECK(!usb_endpoint_);
  usb_endpoint_ = CreateEcUsbEndpoint();
  capabilities_ = RgbKeyboardCapabilities::kNone;

  LOG(INFO) << "Checking RgbKeyboardCapabilities by trying to set all keys to "
            << CreateRgbLogString(/*r=*/0, /*g=*/0, /*b=*/0);
  auto command = ec::RgbkbdCommand::Create(EC_RGBKBD_SUBCMD_CLEAR, {0, 0, 0});
  if (usb_endpoint_ && command->Run(*usb_endpoint_)) {
    capabilities_ = RgbKeyboardCapabilities::kIndividualKey;
  } else {
    // Try and use a FD if USB fails.
    ec_fd_ = CreateFileDescriptorForEc();
    if (ec_fd_.is_valid() && command->Run(ec_fd_.get())) {
      capabilities_ = RgbKeyboardCapabilities::kFiveZone;
    }
  }

  DCHECK(capabilities_.has_value());
  LogSupportType(capabilities_.value());
  return capabilities_.value();
}
}  // namespace rgbkbd
