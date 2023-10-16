// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_capport_proxy.h"

namespace shill {

MockCapportProxy::MockCapportProxy() : CapportProxy("", nullptr) {}
MockCapportProxy::~MockCapportProxy() = default;

}  // namespace shill
