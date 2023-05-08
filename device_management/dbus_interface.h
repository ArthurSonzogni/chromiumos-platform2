// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEVICE_MANAGEMENT_DBUS_INTERFACE_H_
#define DEVICE_MANAGEMENT_DBUS_INTERFACE_H_

namespace device_management {

inline constexpr char kDeviceManagementInterface[] =
    "org.chromium.DeviceManagement";
inline constexpr char kDeviceManagementServicePath[] =
    "/org/chromium/DeviceManagement";
inline constexpr char kDeviceManagementServiceName[] =
    "org.chromium.DeviceManagement";

// Methods exported by device_management.
inline constexpr char kGetFirmwareManagementParameters[] =
    "GetFirmwareManagementParameters";
inline constexpr char kSetFirmwareManagementParameters[] =
    "SetFirmwareManagementParameters";
inline constexpr char kRemoveFirmwareManagementParameters[] =
    "RemoveFirmwareManagementParameters";

}  // namespace device_management

#endif  // DEVICE_MANAGEMENT_DBUS_INTERFACE_H_
