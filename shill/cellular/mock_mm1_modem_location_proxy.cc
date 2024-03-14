// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/mock_mm1_modem_location_proxy.h"

#include "shill/testing.h"

using testing::_;
using testing::Invoke;
using testing::WithArgs;

namespace shill {
namespace mm1 {

MockModemLocationProxy::MockModemLocationProxy() {
  ON_CALL(*this, Setup(_, _, _, _))
      .WillByDefault(
          WithArgs<2>(Invoke(ReturnOperationFailed<ResultCallback>)));
  ON_CALL(*this, GetLocation(_, _))
      .WillByDefault(
          WithArgs<0>(Invoke(ReturnOperationFailed<BrilloAnyCallback>)));
}

MockModemLocationProxy::~MockModemLocationProxy() = default;

}  // namespace mm1
}  // namespace shill
