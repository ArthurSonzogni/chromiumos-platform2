// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_MOCK_MOJO_RELAY_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_MOCK_MOJO_RELAY_H_

#include <gmock/gmock.h>

#include "diagnostics/cros_healthd/utils/mojo_relay.h"

namespace diagnostics {

template <typename Interface>
class MockMojoRelay : public MojoRelay<Interface> {
  using MojoRelay<Interface>::MojoRelay;

 public:
  MOCK_METHOD(typename Interface::Proxy_*, Get, (), (override));
  MOCK_METHOD(bool, IsBound, (), (override));
  MOCK_METHOD(void, Bind, (mojo::PendingRemote<Interface>), (override));
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_MOCK_MOJO_RELAY_H_
