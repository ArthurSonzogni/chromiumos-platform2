// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/delegate/utils/display_util_factory_impl.h"

#include <memory>

#include "diagnostics/cros_healthd/delegate/utils/display_util_impl.h"

namespace diagnostics {

DisplayUtilFactoryImpl::DisplayUtilFactoryImpl() = default;

DisplayUtilFactoryImpl::~DisplayUtilFactoryImpl() = default;

std::unique_ptr<DisplayUtil> DisplayUtilFactoryImpl::Create() {
  return DisplayUtilImpl::Create();
}

}  // namespace diagnostics
