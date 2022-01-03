// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_IMPL_H_

#include "diagnostics/cros_healthd/system/libdrm_util.h"

namespace diagnostics {

class LibdrmUtilImpl : public LibdrmUtil {
 public:
  LibdrmUtilImpl();
  LibdrmUtilImpl(const LibdrmUtilImpl& oth) = delete;
  LibdrmUtilImpl(LibdrmUtilImpl&& oth) = delete;
  ~LibdrmUtilImpl() override;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_IMPL_H_
