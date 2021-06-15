// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_CONSTANTS_H_
#define DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_CONSTANTS_H_

namespace diagnostics {

// Relative paths to cached VPD information.
inline constexpr char kRelativePathVpdRo[] = "sys/firmware/vpd/ro/";
inline constexpr char kRelativePathVpdRw[] = "sys/firmware/vpd/rw/";

// Files related to cached VPD information.
inline constexpr char kFileNameActivateDate[] = "ActivateDate";
inline constexpr char kFileNameMfgDate[] = "mfg_date";
inline constexpr char kFileNameModelName[] = "model_name";
inline constexpr char kFileNameRegion[] = "region";
inline constexpr char kFileNameSerialNumber[] = "serial_number";
inline constexpr char kFileNameSkuNumber[] = "sku_number";

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_FETCHERS_SYSTEM_FETCHER_CONSTANTS_H_
