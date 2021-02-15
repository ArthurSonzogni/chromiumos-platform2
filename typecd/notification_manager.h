// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_NOTIFICATION_MANAGER_H_
#define TYPECD_NOTIFICATION_MANAGER_H_

namespace typecd {

enum class ConnectNotification {
  kTBTOnly = 0,
  kTBTDP = 1,
};

class NotificationManager {
 public:
  NotificationManager() = default;

  void NotifyConnected(ConnectNotification notify);
};

}  // namespace typecd

#endif  // TYPECD_NOTIFICATION_MANAGER_H_
