// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_UPSTART_MOCK_UPSTART_PROXY_H_
#define SHILL_UPSTART_MOCK_UPSTART_PROXY_H_

#include <string>
#include <vector>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/upstart/upstart_proxy_interface.h"

namespace shill {

class MockUpstartProxy : public UpstartProxyInterface {
 public:
  MockUpstartProxy();
  ~MockUpstartProxy() override;

  MOCK_METHOD3(EmitEvent,
               void(const std::string& name,
                    const std::vector<std::string>& env,
                    bool wait));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockUpstartProxy);
};

}  // namespace shill

#endif  // SHILL_UPSTART_MOCK_UPSTART_PROXY_H_
