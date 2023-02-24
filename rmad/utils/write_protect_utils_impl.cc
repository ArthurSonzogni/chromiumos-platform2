// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/utils/write_protect_utils_impl.h"

#include <memory>
#include <utility>

#include "rmad/utils/crossystem_utils_impl.h"
#include "rmad/utils/ec_utils_impl.h"
#include "rmad/utils/flashrom_utils_impl.h"

namespace rmad {

WriteProtectUtilsImpl::WriteProtectUtilsImpl()
    : crossystem_utils_(std::make_unique<CrosSystemUtilsImpl>()),
      ec_utils_(std::make_unique<EcUtilsImpl>()),
      flashrom_utils_(std::make_unique<FlashromUtilsImpl>()) {}

WriteProtectUtilsImpl::WriteProtectUtilsImpl(
    std::unique_ptr<CrosSystemUtils> crossystem_utils,
    std::unique_ptr<EcUtils> ec_utils,
    std::unique_ptr<FlashromUtils> flashrom_utils)
    : crossystem_utils_(std::move(crossystem_utils)),
      ec_utils_(std::move(ec_utils)),
      flashrom_utils_(std::move(flashrom_utils)) {}

}  // namespace rmad
