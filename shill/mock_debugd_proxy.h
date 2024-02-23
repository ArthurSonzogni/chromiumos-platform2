// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_DEBUGD_PROXY_H_
#define SHILL_MOCK_DEBUGD_PROXY_H_

#include <debugd/dbus-proxies.h>
#include <gmock/gmock.h>

#include "shill/debugd_proxy_interface.h"

namespace shill {

class MockDebugdProxy : public DebugdProxyInterface {
 public:
  MockDebugdProxy() = default;
  MockDebugdProxy(const MockDebugdProxy&) = delete;
  MockDebugdProxy& operator=(const MockDebugdProxy&) = delete;

  ~MockDebugdProxy() override = default;

  MOCK_METHOD(void,
              GenerateFirmwareDump,
              (const debugd::FirmwareDumpType&),
              (override));
};

}  // namespace shill

#endif  // SHILL_MOCK_DEBUGD_PROXY_H_
