// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOJOM_SHILL_MOJOM_PORTAL_NOTIFICATION_EVENT_MOJOM_TRAITS_H_
#define SHILL_MOJOM_SHILL_MOJOM_PORTAL_NOTIFICATION_EVENT_MOJOM_TRAITS_H_

#include <mojo/public/cpp/bindings/enum_traits.h>

#include "shill/mojom/portal.mojom.h"
#include "shill/network/portal_notification_event.h"

namespace mojo {

template <>
struct EnumTraits<chromeos::connectivity::mojom::NotificationEvent,
                  shill::PortalNotificationEvent> {
  static chromeos::connectivity::mojom::NotificationEvent ToMojom(
      shill::PortalNotificationEvent input) {
    switch (input) {
      case shill::PortalNotificationEvent::kShown:
        return chromeos::connectivity::mojom::NotificationEvent::kShown;
      case shill::PortalNotificationEvent::kClicked:
        return chromeos::connectivity::mojom::NotificationEvent::kClicked;
      case shill::PortalNotificationEvent::kDismissed:
        return chromeos::connectivity::mojom::NotificationEvent::kDismissed;
    }
  }

  static bool FromMojom(chromeos::connectivity::mojom::NotificationEvent input,
                        shill::PortalNotificationEvent* output) {
    switch (input) {
      case chromeos::connectivity::mojom::NotificationEvent::kUnknown:
        return false;
      case chromeos::connectivity::mojom::NotificationEvent::kShown:
        *output = shill::PortalNotificationEvent::kShown;
        break;
      case chromeos::connectivity::mojom::NotificationEvent::kClicked:
        *output = shill::PortalNotificationEvent::kClicked;
        break;
      case chromeos::connectivity::mojom::NotificationEvent::kDismissed:
        *output = shill::PortalNotificationEvent::kDismissed;
        break;
    }
    return true;
  }
};

}  // namespace mojo
#endif  // SHILL_MOJOM_SHILL_MOJOM_PORTAL_NOTIFICATION_EVENT_MOJOM_TRAITS_H_
