// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSTEM_API_DBUS_ARC_MANAGER_DBUS_CONSTANTS_H_
#define SYSTEM_API_DBUS_ARC_MANAGER_DBUS_CONSTANTS_H_

namespace arc_manager {

inline constexpr char kArcManagerInterface[] = "org.chromium.ArcManager";
inline constexpr char kArcManagerServicePath[] = "/org/chromium/ArcManager";
inline constexpr char kArcManagerServiceName[] = "org.chromium.ArcManager";

// Methods
inline constexpr char kArcManagerOnUserSessionStarted[] =
    "OnUserSessionStarted";
inline constexpr char kArcManagerEmitArcBooted[] = "EmitArcBooted";
inline constexpr char kArcManagerEnableAdbSideload[] = "EnableAdbSideload";
inline constexpr char kArcManagerQueryAdbSideload[] = "QueryAdbSideload";

// Note: For historical reasons, currently ArcManager uses the error code
// defined in login_manager namespace for SessionManager.
// TODO(b:390297821): Define ArcManager's error status and migrate.

}  // namespace arc_manager

#endif  // SYSTEM_API_DBUS_ARC_MANAGER_DBUS_CONSTANTS_H_
