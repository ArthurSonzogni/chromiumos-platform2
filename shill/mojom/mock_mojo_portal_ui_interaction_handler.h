// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOJOM_MOCK_MOJO_PORTAL_UI_INTERACTION_HANDLER_H_
#define SHILL_MOJOM_MOCK_MOJO_PORTAL_UI_INTERACTION_HANDLER_H_

#include <chromeos/net-base/http_url.h>
#include <gmock/gmock.h>

#include "shill/mojom/mojo_portal_ui_interaction_handler.h"
#include "shill/network/portal_notification_event.h"

namespace shill {

class MockMojoPortalUIInteractionHandler
    : public MojoPortalUIInteractionHandler {
 public:
  MockMojoPortalUIInteractionHandler();
  ~MockMojoPortalUIInteractionHandler() override;

  MOCK_METHOD(void,
              OnNotificationEvent,
              (int32_t, PortalNotificationEvent),
              (override));
  MOCK_METHOD(void,
              OnSigninPageShown,
              (int32_t, const net_base::HttpUrl&),
              (override));
  MOCK_METHOD(void, OnSigninPageLoaded, (int32_t, int32_t), (override));
  MOCK_METHOD(void, OnSigninPageClosed, (int32_t), (override));
};

}  // namespace shill
#endif  // SHILL_MOJOM_MOCK_MOJO_PORTAL_UI_INTERACTION_HANDLER_H_
