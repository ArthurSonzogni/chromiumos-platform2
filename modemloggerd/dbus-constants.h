// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODEMLOGGERD_DBUS_CONSTANTS_H_
#define MODEMLOGGERD_DBUS_CONSTANTS_H_

namespace modemloggerd {

// Modemloggerd D-Bus service identifier.
const char kModemloggerdServiceName[] = "org.chromium.Modemloggerd";

// Error codes.
const char kErrorUnknown[] = "org.chromium.Modemloggerd.Error.Unknown";
const char kErrorOperationFailed[] =
    "org.chromium.Modemloggerd.Error.OperationFailed";

}  // namespace modemloggerd

#endif  // MODEMLOGGERD_DBUS_CONSTANTS_H_
