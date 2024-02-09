// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_MOCK_PROXY_RESOLVER_H_
#define UPDATE_ENGINE_COMMON_MOCK_PROXY_RESOLVER_H_

#include <string>

#include <gmock/gmock.h>

#include "update_engine/common/proxy_resolver.h"

namespace chromeos_update_engine {

class MockProxyResolver : public ProxyResolver {
 public:
  MOCK_METHOD(ProxyRequestId,
              GetProxiesForUrl,
              (const std::string&, ProxiesResolvedFn),
              (override));
  MOCK_METHOD(bool, CancelProxyRequest, (ProxyRequestId), (override));
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_MOCK_PROXY_RESOLVER_H_
