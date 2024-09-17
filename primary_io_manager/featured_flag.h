// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRIMARY_IO_MANAGER_FEATURED_FLAG_H_
#define PRIMARY_IO_MANAGER_FEATURED_FLAG_H_

#include "featured/c_feature_library.h"

namespace primary_io_manager {

// static
// TODO(b/366218789): enable by default once configurable via policy.
const struct VariationsFeature kChromeboxUsbPassthroughRestrictions {
  .name = "CrOSLateBootChromeboxUsbPassthroughRestrictions",
  .default_state = FEATURE_DISABLED_BY_DEFAULT,
};

}  // namespace primary_io_manager

#endif  // PRIMARY_IO_MANAGER_FEATURED_FLAG_H_
