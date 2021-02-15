// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/notification_manager.h"

#include <string>

#include <base/logging.h>

namespace {

// TODO(b/179711675): Get rid of this when you implement the Chrome i/f.
std::string ToString(typecd::ConnectNotification notify) {
  switch (notify) {
    case typecd::ConnectNotification::kTBTOnly:
      return "TBT Only";
    case typecd::ConnectNotification::kTBTDP:
      return "TBT+DP";
  }
}

}  // namespace

namespace typecd {

void NotificationManager::NotifyConnected(ConnectNotification notify) {
  // TODO(b/179711675): Surface this to Chrome either by emitting
  // a D-Bus or calling a D-Bus method.
  LOG(INFO) << "Sending connect notification: " << ToString(notify);
}

}  // namespace typecd
