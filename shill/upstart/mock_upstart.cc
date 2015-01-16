// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/upstart/mock_upstart.h"

namespace shill {

MockUpstart::MockUpstart(ProxyFactory *proxy_factory)
    : Upstart(proxy_factory) {}

MockUpstart::~MockUpstart() {}

}  // namespace shill
