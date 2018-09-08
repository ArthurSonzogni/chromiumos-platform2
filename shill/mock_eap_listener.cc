// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/mock_eap_listener.h"

namespace shill {

MockEapListener::MockEapListener() : EapListener(nullptr, 0) {}

MockEapListener::~MockEapListener() {}

}  // namespace shill
