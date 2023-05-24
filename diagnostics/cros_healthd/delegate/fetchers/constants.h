// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_FETCHERS_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_FETCHERS_CONSTANTS_H_

namespace diagnostics {

constexpr char kSubsystemInput[] = "input";

namespace touchpad {
inline constexpr char kUdevPropertyIdInputTouchpad[] = "ID_INPUT_TOUCHPAD";
inline constexpr char kUdevPropertyIdBus[] = "ID_BUS";
inline constexpr char kUdevPropertyDevname[] = "DEVNAME";
inline constexpr char kUdevPropertyDevpath[] = "DEVPATH";
inline constexpr char kUdevPropertyMajor[] = "MAJOR";
inline constexpr char kUdevPropertyMinor[] = "MINOR";
}  // namespace touchpad

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_FETCHERS_CONSTANTS_H_
