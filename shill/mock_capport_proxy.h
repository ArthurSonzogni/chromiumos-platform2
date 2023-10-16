// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_CAPPORT_PROXY_H_
#define SHILL_MOCK_CAPPORT_PROXY_H_

#include "shill/capport_proxy.h"

#include <gmock/gmock.h>

namespace shill {

class MockCapportProxy : public CapportProxy {
 public:
  MockCapportProxy();
  ~MockCapportProxy() override;

  MOCK_METHOD(void, SendRequest, (StatusCallback callback), (override));
  MOCK_METHOD(bool, IsRunning, (), (const, override));
};

}  // namespace shill
#endif  // SHILL_MOCK_CAPPORT_PROXY_H_
