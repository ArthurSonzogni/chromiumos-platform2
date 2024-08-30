// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_DISPLAY_UTIL_FACTORY_H_
#define DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_DISPLAY_UTIL_FACTORY_H_

#include <memory>

namespace diagnostics {
class DisplayUtil;

class DisplayUtilFactory {
 public:
  virtual std::unique_ptr<DisplayUtil> Create() = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_DELEGATE_UTILS_DISPLAY_UTIL_FACTORY_H_
