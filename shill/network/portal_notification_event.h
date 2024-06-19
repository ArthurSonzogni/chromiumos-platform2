// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_NETWORK_PORTAL_NOTIFICATION_EVENT_H_
#define SHILL_NETWORK_PORTAL_NOTIFICATION_EVENT_H_

namespace shill {

// The events for the portal notification UI component. It should be sync with
// chromeos::connectivity::mojom::NotificationEvent.
enum class PortalNotificationEvent {
  kShown,
  kClicked,
  kDismissed,
};

}  // namespace shill
#endif  // SHILL_NETWORK_PORTAL_NOTIFICATION_EVENT_H_
