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

void LogSupportType(RgbKeyboardCapabilities capabilities) {
  switch (capabilities) {
    case RgbKeyboardCapabilities::kNone:
      LOG(INFO) << "Device does not support an internal RGB keyboard";
      break;
    case RgbKeyboardCapabilities::kFourZoneFortyLed:
      LOG(INFO) << "Device supports four zone - fourty led keyboard";
      break;
    case RgbKeyboardCapabilities::kIndividualKey:
      LOG(INFO) << "Device supports per-key keyboard over USB";
      break;
    case RgbKeyboardCapabilities::kFourZoneTwelveLed:
      LOG(INFO) << "Device supports four zone - twelve led keyboard";
      break;
    case RgbKeyboardCapabilities::kFourZoneFifteenLed:
      LOG(INFO) << "Device supports four zone - fifteen led keyboard";
      break;
  }
}

std::unique_ptr<ec::EcUsbEndpointInterface> CreateEcUsbEndpoint() {
  auto endpoint = std::make_unique<ec::EcUsbEndpoint>();
  return endpoint->Init(ec::kUsbVidGoogle, ec::kUsbPidCrosEc)
             ? std::move(endpoint)
             : std::unique_ptr<ec::EcUsbEndpoint>();
}

base::ScopedFD CreateFileDescriptorForEc() {
  auto raw_fd = open(kEcPath, O_RDWR | O_CLOEXEC);
  if (raw_fd == -1) {
    LOG(ERROR) << "Failed to open FD for EC with errno=" << errno;
    return base::ScopedFD();
  }

  return base::ScopedFD(raw_fd);
}

}  // namespace

bool InternalRgbKeyboard::SetKeyColor(uint32_t key,
                                      uint8_t r,
                                      uint8_t g,
                                      uint8_t b) {
  struct rgb_s color = {r, g, b};
  ec::RgbkbdSetColorCommand command(key, std::vector<struct rgb_s>{color});
  const bool success = RunEcCommand(command);

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
  struct rgb_s color = {r, g, b};
  const bool success =
      RunEcCommand(*ec::RgbkbdCommand::Create(EC_RGBKBD_SUBCMD_CLEAR, color));

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
  RgbKeyboardCapabilities capabilities = RgbKeyboardCapabilities::kNone;

  LOG(INFO) << "Checking RgbKeyboardCapabilities by trying to set all keys to "
            << CreateRgbLogString(/*r=*/0, /*g=*/0, /*b=*/0);
  auto command = ec::RgbkbdCommand::Create(EC_RGBKBD_SUBCMD_CLEAR, {0, 0, 0});

  // TODO(dpad): Replace CLEAR command with GET_CONFIG command once available
  // on all devices. Deducing communication type will still be needed as
  // GET_CONFIG API still needs either USB or FileDescriptor param
  if (SetCommunicationType(*command)) {
    if (communication_type_ == CommunicationType::kUsb) {
      capabilities = RgbKeyboardCapabilities::kIndividualKey;
    } else if (communication_type_ == CommunicationType::kFileDescriptor) {
      capabilities = RgbKeyboardCapabilities::kFourZoneFortyLed;
    }
  }

  LogSupportType(capabilities);
  return capabilities;
}

template <typename T, typename U>
bool InternalRgbKeyboard::SetCommunicationType(ec::EcCommand<T, U>& command) {
  LOG(INFO) << "Deducing Communication type";

  usb_endpoint_ = CreateEcUsbEndpoint();
  if (usb_endpoint_ && command.Run(*usb_endpoint_)) {
    LOG(INFO) << "Internal RGB Keyboard communicates over USB";
    communication_type_ = CommunicationType::kUsb;
    return true;
  }

  ec_fd_ = CreateFileDescriptorForEc();
  if (ec_fd_.is_valid() && command.Run(ec_fd_.get())) {
    LOG(INFO) << "Internal RGB Keyboard communicates over FD";
    communication_type_ = CommunicationType::kFileDescriptor;
    return true;
  }

  LOG(ERROR) << "Failed to deduce communication type for internal RGB Keyboard";
  return false;
}

template <typename T, typename U>
bool InternalRgbKeyboard::RunEcCommand(ec::EcCommand<T, U>& command) {
  if (!communication_type_) {
    LOG(ERROR) << "Could not run EC command, Internal RGB Keyboard has no "
                  "communication type set";
    return false;
  }

  switch (communication_type_.value()) {
    case CommunicationType::kUsb:
      DCHECK(usb_endpoint_);
      return command.Run(*usb_endpoint_);
    case CommunicationType::kFileDescriptor:
      DCHECK(ec_fd_.is_valid());
      return command.Run(ec_fd_.get());
  }
}

}  // namespace rgbkbd
