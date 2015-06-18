// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_UPSTART_MOCK_UPSTART_H_
#define SHILL_UPSTART_MOCK_UPSTART_H_

#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/upstart/upstart.h"

namespace shill {

class ProxyFactory;

class MockUpstart : public Upstart {
 public:
  explicit MockUpstart(ProxyFactory* proxy_factory);
  ~MockUpstart() override;

  MOCK_METHOD0(NotifyDisconnected, void());
  MOCK_METHOD0(NotifyConnected, void());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockUpstart);
};

}  // namespace shill

#endif  // SHILL_UPSTART_MOCK_UPSTART_H_
