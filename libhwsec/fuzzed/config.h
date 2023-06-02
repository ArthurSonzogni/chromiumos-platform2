// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FUZZED_CONFIG_H_
#define LIBHWSEC_FUZZED_CONFIG_H_

#include <memory>
#include <string>
#include <utility>

#include <fuzzer/FuzzedDataProvider.h>

#include "libhwsec/fuzzed/basic_objects.h"
#include "libhwsec/structures/device_config.h"

using Mode = hwsec::DeviceConfigSettings::BootModeSetting::Mode;

namespace hwsec {

template <>
struct FuzzedObject<Mode> {
  Mode operator()(FuzzedDataProvider& provider) const {
    return Mode{
        .developer_mode = provider.ConsumeBool(),
        .recovery_mode = provider.ConsumeBool(),
        .verified_firmware = provider.ConsumeBool(),
    };
  }
};

}  // namespace hwsec

#endif  // LIBHWSEC_FUZZED_CONFIG_H_
