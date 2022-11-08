// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FUZZED_IFX_INFO_H_
#define LIBHWSEC_FUZZED_IFX_INFO_H_

#include <type_traits>

#include <brillo/secure_blob.h>
#include <fuzzer/FuzzedDataProvider.h>

#include "libhwsec/fuzzed/basic_objects.h"
#include "libhwsec/structures/ifx_info.h"

namespace hwsec {

template <>
struct FuzzedObject<IFXFieldUpgradeInfo::FirmwarePackage> {
  IFXFieldUpgradeInfo::FirmwarePackage operator()(
      FuzzedDataProvider& provider) const {
    return IFXFieldUpgradeInfo::FirmwarePackage{
        .package_id = FuzzedObject<uint32_t>()(provider),
        .version = FuzzedObject<uint32_t>()(provider),
        .stale_version = FuzzedObject<uint32_t>()(provider),
    };
  }
};

template <>
struct FuzzedObject<IFXFieldUpgradeInfo> {
  IFXFieldUpgradeInfo operator()(FuzzedDataProvider& provider) const {
    return IFXFieldUpgradeInfo{
        .max_data_size = FuzzedObject<uint16_t>()(provider),
        .bootloader =
            FuzzedObject<IFXFieldUpgradeInfo::FirmwarePackage>()(provider),
        .firmware =
            {
                FuzzedObject<IFXFieldUpgradeInfo::FirmwarePackage>()(provider),
                FuzzedObject<IFXFieldUpgradeInfo::FirmwarePackage>()(provider),
            },
        .status = FuzzedObject<uint16_t>()(provider),
        .process_fw =
            FuzzedObject<IFXFieldUpgradeInfo::FirmwarePackage>()(provider),
        .field_upgrade_counter = FuzzedObject<uint16_t>()(provider),
    };
  }
};

}  // namespace hwsec

#endif  // LIBHWSEC_FUZZED_IFX_INFO_H_
