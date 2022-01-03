// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_LIBDRM_UTIL_H_
#define DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_LIBDRM_UTIL_H_

#include "diagnostics/cros_healthd/system/libdrm_util.h"

namespace diagnostics {

class FakeLibdrmUtil : public LibdrmUtil {
 public:
  FakeLibdrmUtil() = default;
  FakeLibdrmUtil(const FakeLibdrmUtil& oth) = default;
  FakeLibdrmUtil(FakeLibdrmUtil&& oth) = default;
  ~FakeLibdrmUtil() override = default;

  bool Initialize() override;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_SYSTEM_FAKE_LIBDRM_UTIL_H_
