// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_CONSTANTS_H_

namespace cpu_msr {
// The msr address for IA32_TME_CAPABILITY (0x981), used to report tme telemetry
// data.
inline constexpr uint32_t kIA32TmeCapability = 0x981;
// The msr address for IA32_TME_ACTIVATE_MSR (0x982), used to report tme
// telemetry data.
inline constexpr uint32_t kIA32TmeActivate = 0x982;

}  // namespace cpu_msr

#endif  // DIAGNOSTICS_CROS_HEALTHD_EXECUTOR_CONSTANTS_H_
