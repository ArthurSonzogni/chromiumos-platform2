// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_RESOLVER_H_
#define SHILL_MOCK_RESOLVER_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "shill/resolver.h"

namespace shill {

class MockResolver : public Resolver {
 public:
  MockResolver();
  MockResolver(const MockResolver&) = delete;
  MockResolver& operator=(const MockResolver&) = delete;

  ~MockResolver() override;

  MOCK_METHOD(bool,
              SetDNSProxyAddresses,
              (const std::vector<std::string>&),
              (override));
};

}  // namespace shill

#endif  // SHILL_MOCK_RESOLVER_H_
