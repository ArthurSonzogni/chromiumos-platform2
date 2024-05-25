// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_GUEST_IPV6_SERVICE_H_
#define PATCHPANEL_MOCK_GUEST_IPV6_SERVICE_H_

#include <base/files/scoped_file.h>
#include <base/functional/callback.h>
#include <base/functional/callback_helpers.h>
#include <gmock/gmock.h>

#include "patchpanel/guest_ipv6_service.h"
#include "patchpanel/shill_client.h"

namespace patchpanel {

class MockGuestIPv6Service : public GuestIPv6Service {
 public:
  MockGuestIPv6Service();
  explicit MockGuestIPv6Service(const MockGuestIPv6Service&) = delete;
  MockGuestIPv6Service& operator=(const MockGuestIPv6Service&) = delete;
  virtual ~MockGuestIPv6Service();

  MOCK_METHOD(void, StopUplink, (const ShillClient::Device&), (override));
  MOCK_METHOD(void,
              OnUplinkIPv6Changed,
              (const ShillClient::Device&),
              (override));
  MOCK_METHOD(void,
              UpdateUplinkIPv6DNS,
              (const ShillClient::Device&),
              (override));
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_GUEST_IPV6_SERVICE_H_
