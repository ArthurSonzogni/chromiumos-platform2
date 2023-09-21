// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/check.h>
#include <base/containers/span.h>

#include "libec/i2c_passthru_command.h"

namespace ec {

base::span<const uint8_t> I2cPassthruCommand::RespData() const {
  if (I2cStatus())
    return {};
  CHECK(RespSize() - realsizeof<decltype(Resp()->resp)> >= 0);
  return {Resp()->data.begin(),
          RespSize() - realsizeof<decltype(Resp()->resp)>};
}

}  // namespace ec
