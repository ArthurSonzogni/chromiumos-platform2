// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_DISPLAY_UTIL_FACTORY_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_DISPLAY_UTIL_FACTORY_IMPL_H_

#include <memory>

#include "diagnostics/cros_healthd/delegate/utils/display_util_factory.h"

namespace diagnostics {

class DisplayUtilFactoryImpl : public DisplayUtilFactory {
 public:
  DisplayUtilFactoryImpl();
  DisplayUtilFactoryImpl(const DisplayUtilFactoryImpl&) = delete;
  DisplayUtilFactoryImpl(DisplayUtilFactoryImpl&&) = delete;
  ~DisplayUtilFactoryImpl();

  // DisplayUtilFactory overrides:
  std::unique_ptr<DisplayUtil> Create() override;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_DISPLAY_UTIL_FACTORY_IMPL_H_
