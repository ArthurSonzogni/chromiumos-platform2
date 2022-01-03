// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_H_

namespace diagnostics {

// Interface for accessing the libdrm library.
class LibdrmUtil {
 public:
  virtual ~LibdrmUtil() = default;

  // Initialize the util object.
  virtual bool Initialize() = 0;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_LIBDRM_UTIL_H_
