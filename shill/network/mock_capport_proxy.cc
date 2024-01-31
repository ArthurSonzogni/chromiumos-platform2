// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/network/mock_capport_proxy.h"

#include <net-base/http_url.h>

namespace shill {

MockCapportProxy::MockCapportProxy()
    : CapportProxy(*net_base::HttpUrl::CreateFromString(
                       "https://example.org/portal.html"),
                   nullptr) {}
MockCapportProxy::~MockCapportProxy() = default;

MockCapportProxyFactory::MockCapportProxyFactory() = default;

MockCapportProxyFactory::~MockCapportProxyFactory() = default;

}  // namespace shill
