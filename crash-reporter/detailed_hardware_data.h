// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_DETAILED_HARDWARE_DATA_H_
#define CRASH_REPORTER_DETAILED_HARDWARE_DATA_H_

#include <map>
#include <string>

#include <policy/device_policy.h>

namespace detailed_hardware_data {

// This is the manufacturer/model info read from dmi for non-chromebooks.
// It's okay to send this info in crashes, as detailed in
// https://support.google.com/chromebook/answer/96817
// "Your device's operating system, manufacturer, and model".
std::map<std::string, std::string> DmiModelInfo();

// This is more detailed component info, which can be useful on ChromeOS Flex
// where the board/HWID doesn't convey any information about hardware.
std::map<std::string, std::string> FlexComponentInfo();

// Check whether we're allowed to include component info.
// Component info can potentially include uniquely identifying information,
// so users/administrators can control whether it's sent.
// |device_policy| must already be loaded.
bool FlexComponentInfoAllowedByPolicy(
    const policy::DevicePolicy& device_policy);

}  // namespace detailed_hardware_data

#endif  // CRASH_REPORTER_DETAILED_HARDWARE_DATA_H_
